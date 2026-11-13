#include "audio.h"

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>
#include <ogc/lwp.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tremor/ivorbisfile.h>
#include <unistd.h>

#define MUSIC_VOICE 0
#define NBUF 6
#define BUF_FRAMES 2048
#define BUF_BYTES (BUF_FRAMES * 4)

typedef struct {
    FILE *f;
    OggVorbis_File vf;
    bool open, eof;
    int channels;
    int src_rate;
    double phase;      /* position of the next output frame, in source frames after `carry` */
    bool primed;
    s16 carry[2];
    float vol, vol_now;
} track;

typedef struct {
    s16 *data;
    volatile bool filled;
    u64 frame0;
    int frames;
} mixbuf;

static track tracks[MUSIC_MAX_TRACKS];
static int ntracks;
static int rate = 44100;
static int main_t; /* the track that sets timing: the first one that opened */
static bool loop_music;
static double length_ms;

static mixbuf bufs[NBUF];
static volatile int write_idx, read_idx;
static volatile int playing = -1, queued = -1;
static volatile u64 play_start_tick;
static volatile u64 play_start_frame;
static volatile int play_frames;
static volatile u64 end_frame;      /* frame position when nothing is playing */
static volatile bool voice_started, paused = true, decode_eof, finished;
static u64 pause_tick;
static u64 decoded_frames;

static lwp_t decode_thread = LWP_THREAD_NULL;
static volatile bool thread_quit;
static mutex_t music_lock;

static float master_music = 1.0f, master_sfx = 1.0f, music_vol = 1.0f;
static float fade_from, fade_to, fade_time, fade_len;

static s16 *decode_tmp;

/* ---------------- music ---------------- */

static void voice_cb(s32 voice)
{
    (void)voice;
    if (queued >= 0) {
        if (playing >= 0) bufs[playing].filled = false;
        playing = queued;
        queued = -1;
        play_start_tick = gettime();
        play_start_frame = bufs[playing].frame0;
        play_frames = bufs[playing].frames;
    } else if (playing >= 0 && ASND_StatusVoice(MUSIC_VOICE) == SND_WAITING) {
        /* Nothing was queued in time: the voice ran dry. (Right after
         * ASND_SetVoice the callback also comes with nothing queued, but
         * then the first buffer is still playing.) */
        end_frame = bufs[playing].frame0 + bufs[playing].frames;
        bufs[playing].filled = false;
        playing = -1;
    }
    int r = read_idx;
    if (bufs[r].filled && r != playing) {
        if (ASND_AddVoice(MUSIC_VOICE, bufs[r].data, bufs[r].frames * 4) == SND_OK) {
            queued = r;
            read_idx = (r + 1) % NBUF;
        }
    } else if (decode_eof && playing < 0 && queued < 0) {
        finished = true;
    }
}

static int read_raw(track *t, s16 *out, int frames)
{
    /* Fills out with stereo frames; returns frames read (the rest is silence). */
    int got = 0;
    while (got < frames && !t->eof) {
        int want = (frames - got) * t->channels * 2;
        int bs;
        long n = ov_read(&t->vf, (char *)(decode_tmp), want > BUF_BYTES ? BUF_BYTES : want, &bs);
        if (n <= 0) {
            if (n == OV_HOLE) continue;
            t->eof = true;
            break;
        }
        int fr = n / (t->channels * 2);
        if (t->channels == 1) {
            for (int i = 0; i < fr; i++) out[(got + i) * 2] = out[(got + i) * 2 + 1] = decode_tmp[i];
        } else {
            for (int i = 0; i < fr; i++) {
                out[(got + i) * 2] = decode_tmp[i * t->channels];
                out[(got + i) * 2 + 1] = decode_tmp[i * t->channels + 1];
            }
        }
        got += fr;
    }
    if (got < frames) memset(out + got * 2, 0, (frames - got) * 4);
    return got;
}

static s16 *rs_src;

/* Reads `frames` stereo frames at the mix rate, resampling (linear) tracks recorded at another rate. */
static int read_track(track *t, s16 *out, int frames)
{
    if (t->src_rate == rate || t->src_rate <= 0) return read_raw(t, out, frames);
    double ratio = (double)t->src_rate / rate;
    if (!t->primed) {
        s16 f[2];
        if (read_raw(t, f, 1) < 1) {
            memset(out, 0, frames * 4);
            return 0;
        }
        t->carry[0] = f[0];
        t->carry[1] = f[1];
        t->phase = 0;
        t->primed = true;
    }
    int need = (int)(t->phase + frames * ratio) + 2; /* entries in rs_src, carry included */
    if (need > BUF_FRAMES * 3) need = BUF_FRAMES * 3;
    rs_src[0] = t->carry[0];
    rs_src[1] = t->carry[1];
    int got = read_raw(t, rs_src + 2, need - 1);
    if (got == 0) {
        memset(out, 0, frames * 4);
        return 0;
    }
    for (int i = 0; i < frames; i++) {
        double pos = t->phase + i * ratio;
        int idx = (int)pos;
        if (idx > need - 2) idx = need - 2;
        float k = (float)(pos - idx);
        for (int c = 0; c < 2; c++) {
            int a = rs_src[idx * 2 + c], b = rs_src[(idx + 1) * 2 + c];
            out[i * 2 + c] = (s16)(a + (b - a) * k);
        }
    }
    double np = t->phase + frames * ratio;
    int used = (int)np;
    if (used > need - 1) used = need - 1;
    t->carry[0] = rs_src[used * 2];
    t->carry[1] = rs_src[used * 2 + 1];
    t->phase = np - used;
    return frames;
}

static s16 *track_tmp;
static int mix_acc[BUF_FRAMES * 2];

static void decode_one(mixbuf *b)
{
    memset(mix_acc, 0, sizeof(mix_acc));
    int frames_main = 0;
    for (int t = 0; t < ntracks; t++) {
        track *tr = &tracks[t];
        if (!tr->open) continue;
        int got = read_track(tr, track_tmp, BUF_FRAMES);
        if (t == main_t) frames_main = got;
        float v0 = tr->vol_now, v1 = tr->vol;
        tr->vol_now = v1;
        if (v0 <= 0 && v1 <= 0) continue;
        int g0 = (int)(v0 * 256), g1 = (int)(v1 * 256);
        for (int i = 0; i < BUF_FRAMES; i++) {
            int g = g0 + ((g1 - g0) * i) / BUF_FRAMES;
            mix_acc[i * 2] += (track_tmp[i * 2] * g) >> 8;
            mix_acc[i * 2 + 1] += (track_tmp[i * 2 + 1] * g) >> 8;
        }
    }
    if (tracks[main_t].eof && loop_music) {
        for (int t = 0; t < ntracks; t++)
            if (tracks[t].open) {
                ov_pcm_seek(&tracks[t].vf, 0);
                tracks[t].eof = false;
                tracks[t].primed = false;
            }
        /* the rest of this buffer stays silent; a seamless loop is not needed for menu music */
        frames_main = BUF_FRAMES;
    }
    for (int i = 0; i < BUF_FRAMES * 2; i++) {
        int s = mix_acc[i];
        b->data[i] = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
    }
    b->frame0 = decoded_frames;
    b->frames = frames_main > 0 ? frames_main : 0;
    if (b->frames == 0) b->frames = 0;
    decoded_frames += b->frames;
    DCFlushRange(b->data, BUF_BYTES);
    if (!loop_music && tracks[main_t].eof) decode_eof = true;
}

static void *decode_main(void *arg)
{
    (void)arg;
    while (!thread_quit) {
        LWP_MutexLock(music_lock);
        bool did = false;
        if (ntracks > 0 && !decode_eof) {
            int w = write_idx;
            if (!bufs[w].filled && w != playing && w != queued) {
                decode_one(&bufs[w]);
                if (bufs[w].frames > 0) {
                    u32 level = IRQ_Disable();
                    bufs[w].filled = true;
                    write_idx = (w + 1) % NBUF;
                    IRQ_Restore(level);
                }
                did = true;
            }
        }
        /* Start the voice once a couple of buffers are ready. */
        if (ntracks > 0 && !paused && !voice_started) {
            int ready = 0;
            for (int i = 0; i < NBUF; i++) ready += bufs[i].filled;
            if (ready >= 3 || (decode_eof && ready > 0)) {
                int r = read_idx;
                u32 level = IRQ_Disable();
                playing = r;
                queued = -1;
                read_idx = (r + 1) % NBUF;
                play_start_frame = bufs[r].frame0;
                play_frames = bufs[r].frames;
                play_start_tick = gettime();
                voice_started = true;
                IRQ_Restore(level);
                int vol = (int)(255 * master_music * music_vol);
                ASND_SetVoice(MUSIC_VOICE, VOICE_STEREO_16BIT, rate, 0, bufs[r].data, bufs[r].frames * 4, vol, vol,
                              voice_cb);
            } else if (decode_eof && ready == 0) {
                finished = true;
            }
        }
        LWP_MutexUnlock(music_lock);
        /* Always yield so a burst of decoding cannot starve the game thread (frame hitches). */
        usleep(did ? 1000 : 3000);
    }
    return NULL;
}

void audio_init(void)
{
    ASND_Init();
    ASND_Pause(0);
    for (int i = 0; i < NBUF; i++) bufs[i].data = memalign(32, BUF_BYTES);
    decode_tmp = memalign(32, BUF_BYTES * 2);
    track_tmp = memalign(32, BUF_BYTES);
    rs_src = memalign(32, BUF_BYTES * 3 + 64);
    LWP_MutexInit(&music_lock, false);
    thread_quit = false;
    LWP_CreateThread(&decode_thread, decode_main, NULL, NULL, 64 * 1024, 90);
}

void audio_shutdown(void)
{
    music_close();
    thread_quit = true;
    if (decode_thread != LWP_THREAD_NULL) LWP_JoinThread(decode_thread, NULL);
    ASND_End();
}

static void reset_ring(void)
{
    ASND_StopVoice(MUSIC_VOICE);
    u32 level = IRQ_Disable();
    for (int i = 0; i < NBUF; i++) bufs[i].filled = false;
    write_idx = read_idx = 0;
    playing = queued = -1;
    voice_started = false;
    decode_eof = false;
    finished = false;
    IRQ_Restore(level);
}

void music_close(void)
{
    LWP_MutexLock(music_lock);
    reset_ring();
    for (int i = 0; i < ntracks; i++)
        if (tracks[i].open) ov_clear(&tracks[i].vf); /* closes the FILE too */
    memset(tracks, 0, sizeof(tracks));
    ntracks = 0;
    paused = true;
    main_t = 0;
    end_frame = 0;
    decoded_frames = 0;
    length_ms = 0;
    LWP_MutexUnlock(music_lock);
}

bool music_open(const char *const *paths, int count, bool loop)
{
    music_close();
    LWP_MutexLock(music_lock);
    bool any = false;
    for (int i = 0; i < count && i < MUSIC_MAX_TRACKS; i++) {
        track *t = &tracks[i];
        t->vol = t->vol_now = 1.0f;
        if (!paths[i]) continue;
        FILE *f = fopen(paths[i], "rb");
        if (!f) continue;
        if (ov_open(f, &t->vf, NULL, 0) != 0) {
            fclose(f);
            continue;
        }
        vorbis_info *vi = ov_info(&t->vf, -1);
        t->channels = vi->channels;
        t->src_rate = vi->rate;
        if (!any) {
            rate = vi->rate;
            main_t = i;
            length_ms = ov_time_total(&t->vf, -1); /* Tremor: milliseconds */
        }
        t->open = true;
        any = true;
    }
    ntracks = count < MUSIC_MAX_TRACKS ? count : MUSIC_MAX_TRACKS;
    loop_music = loop;
    paused = true;
    end_frame = 0;
    decoded_frames = 0;
    if (!any) ntracks = 0;
    LWP_MutexUnlock(music_lock);
    return any;
}

bool music_is_open(void) { return ntracks > 0; }

void music_play(void)
{
    if (!paused) return;
    if (voice_started) {
        u64 now = gettime();
        play_start_tick += now - pause_tick;
        ASND_PauseVoice(MUSIC_VOICE, 0);
    }
    paused = false;
}

void music_pause(void)
{
    if (paused) return;
    paused = true;
    pause_tick = gettime();
    if (voice_started) ASND_PauseVoice(MUSIC_VOICE, 1);
}

bool music_playing(void) { return !paused && ntracks > 0; }
bool music_finished(void) { return finished; }
double music_length_ms(void) { return length_ms; }

double music_time_ms(void)
{
    if (ntracks == 0) return 0;
    u32 level = IRQ_Disable();
    u64 frame;
    if (!voice_started) {
        frame = end_frame;
    } else if (playing >= 0) {
        u64 now = paused ? pause_tick : gettime();
        u64 el = ticks_to_microsecs(now - play_start_tick);
        u64 adv = el * (u64)rate / 1000000;
        if (adv > (u64)play_frames) adv = play_frames;
        frame = play_start_frame + adv;
    } else {
        frame = end_frame;
    }
    IRQ_Restore(level);
    double ms = (double)frame * 1000.0 / rate;
    if (loop_music && length_ms > 0) {
        while (ms >= length_ms) ms -= length_ms;
    }
    return ms;
}

void music_seek_ms(double ms)
{
    LWP_MutexLock(music_lock);
    bool was_paused = paused;
    reset_ring();
    if (ms < 0) ms = 0;
    for (int i = 0; i < ntracks; i++)
        if (tracks[i].open) {
            ov_time_seek(&tracks[i].vf, (ogg_int64_t)ms);
            tracks[i].eof = false;
            tracks[i].primed = false;
        }
    u64 frame = (u64)(ms * rate / 1000.0);
    if (tracks[main_t].open) frame = ov_pcm_tell(&tracks[main_t].vf);
    decoded_frames = frame;
    end_frame = frame;
    paused = was_paused;
    if (paused) pause_tick = gettime();
    LWP_MutexUnlock(music_lock);
}

static void apply_music_volume(void)
{
    if (voice_started) {
        int v = (int)(255 * master_music * music_vol);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        ASND_ChangeVolumeVoice(MUSIC_VOICE, v, v);
    }
}

void music_set_volume(float v)
{
    music_vol = v;
    fade_len = 0;
    apply_music_volume();
}

void music_set_track_volume(int t, float v)
{
    if (t >= 0 && t < MUSIC_MAX_TRACKS) tracks[t].vol = v;
}

void music_fade(float from, float to, float seconds)
{
    fade_from = from;
    fade_to = to;
    fade_time = 0;
    fade_len = seconds;
    music_vol = from;
    apply_music_volume();
}

void audio_update(float dt)
{
    if (fade_len > 0) {
        fade_time += dt;
        float k = fade_time / fade_len;
        if (k >= 1) {
            k = 1;
            fade_len = 0;
        }
        music_vol = fade_from + (fade_to - fade_from) * k;
    }
    apply_music_volume();
}

void audio_set_master(float music, float sfx)
{
    master_music = music;
    master_sfx = sfx;
    apply_music_volume();
}

/* ---------------- sound effects ---------------- */

struct sound {
    s16 *data;
    int bytes;
    int rate;
};

sound *sound_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    OggVorbis_File vf;
    if (ov_open(f, &vf, NULL, 0) != 0) {
        fclose(f);
        return NULL;
    }
    vorbis_info *vi = ov_info(&vf, -1);
    int ch = vi->channels;
    long total = (long)ov_pcm_total(&vf, -1);
    if (total <= 0) total = vi->rate * 4;
    int cap = (int)((total * 4 + 31) & ~31) + 64;
    s16 *out = memalign(32, cap);
    s16 *tmp = malloc(8192);
    int frames = 0;
    for (;;) {
        int bs;
        long n = ov_read(&vf, (char *)tmp, 8192, &bs);
        if (n == OV_HOLE) continue;
        if (n <= 0) break;
        int fr = n / (ch * 2);
        if ((frames + fr) * 4 > cap - 32) break;
        for (int i = 0; i < fr; i++) {
            out[(frames + i) * 2] = tmp[i * ch];
            out[(frames + i) * 2 + 1] = tmp[i * ch + (ch > 1 ? 1 : 0)];
        }
        frames += fr;
    }
    free(tmp);
    sound *s = malloc(sizeof(*s));
    s->rate = vi->rate;
    ov_clear(&vf);
    s->data = out;
    s->bytes = (frames * 4 + 31) & ~31;
    if (s->bytes > frames * 4) memset((u8 *)out + frames * 4, 0, s->bytes - frames * 4);
    DCFlushRange(out, s->bytes);
    return s;
}

void sound_free(sound *s)
{
    if (!s) return;
    for (int v = 1; v < 16; v++)
        if (ASND_TestPointer(v, s->data) == 1) ASND_StopVoice(v);
    free(s->data);
    free(s);
}

int sound_play(sound *s, float volume)
{
    if (!s || s->bytes == 0) return -1;
    int voice = -1;
    for (int v = 1; v < 16; v++)
        if (ASND_StatusVoice(v) == SND_UNUSED) {
            voice = v;
            break;
        }
    if (voice < 0) return -1;
    int vol = (int)(255 * volume * master_sfx);
    if (vol > 255) vol = 255;
    if (vol < 0) vol = 0;
    ASND_SetVoice(voice, VOICE_STEREO_16BIT, s->rate, 0, s->data, s->bytes, vol, vol, NULL);
    return voice;
}

void sound_stop_all(void)
{
    for (int v = 1; v < 16; v++) ASND_StopVoice(v);
}
