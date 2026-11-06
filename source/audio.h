/* Music streaming and sound effects.
 *
 * The music player decodes up to four Ogg Vorbis tracks at once (the
 * instrumental and each character's vocals), mixes them on a thread and
 * plays the mix through one ASND voice, so the tracks can never drift
 * apart. music_time_ms() is the position of what is coming out of the
 * speakers right now, which the song conductor follows. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

#define MUSIC_MAX_TRACKS 4

void audio_init(void);
void audio_shutdown(void);

/* Opens the tracks (NULL entries are skipped); playback starts paused at 0. */
bool music_open(const char *const *paths, int count, bool loop);
void music_close(void);
bool music_is_open(void);
void music_play(void);
void music_pause(void);
bool music_playing(void);
bool music_finished(void);
void music_seek_ms(double ms);
double music_time_ms(void);
double music_length_ms(void);
void music_set_volume(float v);                 /* master for the music, 0..1 */
void music_set_track_volume(int track, float v); /* per track, 0..1 */
void music_fade(float from, float to, float seconds);
void audio_update(float dt);                     /* runs fades */
void audio_set_master(float music, float sfx);

typedef struct sound sound;
sound *sound_load(const char *path);  /* whole file decoded to memory */
void sound_free(sound *s);
int  sound_play(sound *s, float volume);  /* returns the voice, or -1 */
void sound_stop_all(void);

#endif
