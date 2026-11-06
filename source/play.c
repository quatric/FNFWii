/* PlayState: a song, its stage, the strumlines, scoring and the HUD. */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autotest.h"
#include "game.h"
#include "song.h"
#include "stage.h"

#define STRUMLINE_SIZE 104.0f
#define NOTE_SPACING (STRUMLINE_SIZE + 8)
#define INITIAL_OFFSET (-0.275f * STRUMLINE_SIZE)
#define NUDGE 2.0f
#define PIXELS_PER_MS 0.45f
#define HIT_WINDOW 160.0
#define STRUMLINE_X_OFFSET 48.0f
#define STRUMLINE_Y_OFFSET 24.0f

#define HEALTH_MAX 2.0f
#define HEALTH_SICK 0.03f
#define HEALTH_GOOD 0.015f
#define HEALTH_BAD 0.0f
#define HEALTH_SHIT (-0.02f)
#define HEALTH_HOLD_PER_SEC 0.12f
#define HEALTH_MISS (-0.08f)
#define HEALTH_GHOST_MISS (-0.08f)
#define MISS_SCORE (-100)
#define GHOST_MISS_SCORE (-10)
#define HOLD_SCORE_PER_SEC 250.0f
#define HOLD_DROP_THRESHOLD 160.0

static const char *const dir_names[4] = {"left", "down", "up", "right"};

/* ------------------------------------------------------------ note style */

typedef struct {
    char atlas[128];
    float scale;
    float off[2];
    bool pixel;
    float alpha;
    char keys[16][64];
    char names[16][24];
    int nkeys;
    char sound[128];
} style_part;

typedef struct {
    style_part note, strum, hold, splash, cover[4], count[4], judge[4], num[10];
    char splash_keys[4][2][64];
    int nsplash[4];
} note_style;

static void part_load(style_part *p, const cJSON *e)
{
    memset(p, 0, sizeof(*p));
    p->scale = 1;
    p->alpha = 1;
    if (!e) return;
    strncpy(p->atlas, jstr(e, "atlas", ""), sizeof(p->atlas) - 1);
    p->scale = jnum(e, "scale", 1);
    cJSON *o = cJSON_GetObjectItem(e, "offsets");
    p->off[0] = jidx(o, 0, 0);
    p->off[1] = jidx(o, 1, 0);
    p->pixel = jbool(e, "isPixel", false);
    p->alpha = jnum(e, "alpha", 1);
    strncpy(p->sound, jstr(e, "sound", ""), sizeof(p->sound) - 1);
    cJSON *a, *anims = cJSON_GetObjectItem(e, "anims");
    cJSON_ArrayForEach(a, anims)
    {
        if (p->nkeys >= 16 || !cJSON_IsString(a)) continue;
        strncpy(p->names[p->nkeys], a->string, 23);
        strncpy(p->keys[p->nkeys], a->valuestring, 63);
        p->nkeys++;
    }
}

static const char *part_key(const style_part *p, const char *name)
{
    for (int i = 0; i < p->nkeys; i++)
        if (!strcmp(p->names[i], name)) return p->keys[i];
    return NULL;
}

static void style_load(note_style *ns, const char *id)
{
    memset(ns, 0, sizeof(*ns));
    char rel[96];
    snprintf(rel, sizeof(rel), "notestyles/%s.json", id);
    cJSON *j = fs_json(rel);
    cJSON *fb = fs_json("notestyles/funkin.json");
    if (!j) {
        j = fb;
        fb = NULL;
    }
#define PART(dst, key)                                                     \
    do {                                                                   \
        cJSON *e_ = cJSON_GetObjectItem(j, key);                           \
        if (!e_ || (!cJSON_GetObjectItem(e_, "atlas") && !cJSON_GetObjectItem(e_, "dirs") && \
                    !cJSON_GetObjectItem(e_, "sound")))                    \
            e_ = cJSON_GetObjectItem(fb, key);                             \
        part_load(&(dst), e_);                                             \
    } while (0)
    PART(ns->note, "note");
    PART(ns->strum, "noteStrumline");
    PART(ns->hold, "holdNote");
    static const char *const cd[4] = {"countdownThree", "countdownTwo", "countdownOne", "countdownGo"};
    for (int i = 0; i < 4; i++) PART(ns->count[i], cd[i]);
    static const char *const jd[4] = {"judgementSick", "judgementGood", "judgementBad", "judgementShit"};
    for (int i = 0; i < 4; i++) PART(ns->judge[i], jd[i]);
    for (int i = 0; i < 10; i++) {
        char k[24];
        snprintf(k, sizeof(k), "comboNumber%d", i);
        PART(ns->num[i], k);
    }
    cJSON *sp = cJSON_GetObjectItem(j, "noteSplash");
    if (!cJSON_GetObjectItem(sp, "atlas")) sp = cJSON_GetObjectItem(fb, "noteSplash");
    part_load(&ns->splash, sp);
    cJSON *sd = cJSON_GetObjectItem(sp, "dirs");
    for (int d = 0; d < 4; d++) {
        cJSON *arr = cJSON_GetObjectItem(sd, dir_names[d]);
        cJSON *k;
        cJSON_ArrayForEach(k, arr)
        {
            if (ns->nsplash[d] < 2 && cJSON_IsString(k)) strncpy(ns->splash_keys[d][ns->nsplash[d]++], k->valuestring, 63);
        }
    }
    cJSON *hc = cJSON_GetObjectItem(j, "holdNoteCover");
    if (!cJSON_GetObjectItem(hc, "dirs") || !cJSON_GetArraySize(cJSON_GetObjectItem(hc, "dirs")))
        hc = cJSON_GetObjectItem(fb, "holdNoteCover");
    for (int d = 0; d < 4; d++) {
        cJSON *e = cJSON_GetObjectItem(cJSON_GetObjectItem(hc, "dirs"), dir_names[d]);
        part_load(&ns->cover[d], e);
        cJSON *keys = cJSON_GetObjectItem(e, "keys");
        static const char *const cn[3] = {"start", "hold", "end"};
        for (int i = 0; i < 3 && i < cJSON_GetArraySize(keys); i++) {
            strncpy(ns->cover[d].names[i], cn[i], 23);
            strncpy(ns->cover[d].keys[i], cJSON_GetArrayItem(keys, i)->valuestring, 63);
            ns->cover[d].nkeys = i + 1;
        }
    }
#undef PART
    cJSON_Delete(j);
    if (fb) cJSON_Delete(fb);
}

/* ------------------------------------------------------------ state */

typedef struct {
    const note_data *d;
    bool hit, missed;
    bool holding;      /* sustain being held */
    bool hold_done;
    bool hold_dropped;
} note_state;

typedef struct {
    bool player;
    float x, y;
    sprite strums[4];
    float strum_w[4], strum_h[4];
    float confirm_timer[4];
    bool pressed[4];
    note_state *notes;
    int n;
    int first;          /* first note that may still matter */
    sprite splash[4];
    sprite cover[4];
    int cover_state[4]; /* 0 off, 1 start/hold, 2 end */
    bool visible;
} strumline;

typedef struct {
    sprite spr;
    float vx, vy, ax, ay;
    float delay, life;
    bool used;
} popup;

static struct {
    bool loaded;
    song s;
    stage st;
    character bf, dad, gf;
    note_style ns;
    strumline lines[2]; /* 0 opponent, 1 player */
    atlas *note_atlas;
    int note_frame[4];
    float note_fw[4], note_fh[4];
    atlas *hold_atlas;
    sprite health_bg;
    sprite icon_p1, icon_p2;
    float icon_size1, icon_size2;
    sound *miss_snd[3];
    sound *count_snd[4];
    sound *death_snd;
    sprite count_spr[4];
    float count_alpha[4];
    int count_step;
    popup pops[24];

    double pos;           /* conductor position, ms */
    bool started, ending, finished;
    int last_step, last_beat;
    int next_event;
    float scroll_speed, scroll_target, scroll_from;
    float scroll_t, scroll_dur;

    float health, health_lerp;
    int score, combo, max_combo, misses;
    int tally[5];         /* sick good bad shit miss */
    int total_notes, hit_notes;

    /* camera */
    camera cam, hud;
    float cam_x, cam_y;   /* scroll */
    float follow_x, follow_y;
    float tw_from_x, tw_from_y, tw_to_x, tw_to_y, tw_t, tw_dur;
    char tw_ease[24];
    bool tw_active;
    float stage_zoom, cur_zoom, bop_mult;
    float zoom_from, zoom_to, zoom_t, zoom_dur;
    char zoom_ease[24];
    bool zoom_active;
    float bop_intensity, hud_bop_intensity;
    int zoom_rate, zoom_rate_offset;

    /* substates */
    bool paused;
    int pause_sel;
    float pause_alpha;
    bool dead;
    int death_stage;      /* 0 first death, 1 loop, 2 confirm */
    float death_timer;
    bool results;
    float results_t;
    bool pixel;
    float time;
} P;

static float strum_xpos(int lane) { return lane * NOTE_SPACING; }

/* ------------------------------------------------------------ helpers */

static void icon_load(sprite *s, const character *c, bool player, float *size)
{
    sprite_init(s);
    sprite_load(s, c->icon);
    *size = c->icon_scale;
    s->flip_x = player ^ c->icon_flip;
}

static void strum_play(strumline *l, int d, const char *what, bool force)
{
    sprite *s = &l->strums[d];
    if (!sprite_has_anim(s, what)) return;
    if (!force && !strcmp(sprite_anim_name(s), what)) return;
    sprite_play(s, what, force);
    /* centerOffsets() + centerOrigin() for the frame now showing */
    float fw = sprite_cur_fw(s);
    atlas *at;
    int f = sprite_cur_frame(s, &at, NULL);
    float fh = f >= 0 ? at->frames[f].fh : s->frame_h;
    s->offset_x = (fw - s->width) * 0.5f;
    s->offset_y = (fh - s->height) * 0.5f;
    s->origin_x = fw / 2;
    s->origin_y = fh / 2;
}

static void line_init(strumline *l, bool player)
{
    memset(l, 0, sizeof(*l));
    l->player = player;
    l->visible = true;
    style_part *sp = &P.ns.strum;
    for (int d = 0; d < 4; d++) {
        sprite *s = &l->strums[d];
        sprite_init(s);
        char k[32];
        static const char *const st[4] = {"Static", "Press", "Confirm", "ConfirmHold"};
        static const char *const an[4] = {"static", "press", "confirm", "confirm-hold"};
        for (int i = 0; i < 4; i++) {
            snprintf(k, sizeof(k), "%s%s", dir_names[d], st[i]);
            const char *key = part_key(sp, k);
            if (key) sprite_add_anim(s, an[i], sp->atlas, key, 24, i == 3, 0, 0, false);
        }
        s->scale_x = s->scale_y = sp->scale;
        sprite_play(s, "static", true);
        sprite_update_hitbox(s);
        l->strum_w[d] = s->width;
        l->strum_h[d] = s->height;
        s->sf_x = s->sf_y = 0;

        /* splashes and hold covers */
        sprite_init(&l->splash[d]);
        for (int i = 0; i < P.ns.nsplash[d]; i++) {
            char nm[8];
            snprintf(nm, sizeof(nm), "s%d", i);
            sprite_add_anim(&l->splash[d], nm, P.ns.splash.atlas, P.ns.splash_keys[d][i], 24, false, 0, 0, false);
        }
        l->splash[d].visible = false;
        l->splash[d].alpha = P.ns.splash.alpha;
        sprite_init(&l->cover[d]);
        style_part *cp = &P.ns.cover[d];
        for (int i = 0; i < cp->nkeys; i++)
            sprite_add_anim(&l->cover[d], cp->names[i], cp->atlas, cp->keys[i], 24, i == 1, 0, 0, false);
        l->cover[d].visible = false;
    }
}

static void line_place(strumline *l, float x, float y)
{
    l->x = x;
    l->y = y;
    for (int d = 0; d < 4; d++) {
        sprite *s = &l->strums[d];
        s->x = x + strum_xpos(d) + INITIAL_OFFSET + P.ns.strum.off[0];
        s->y = y + P.ns.strum.off[1];
    }
}

static void line_free(strumline *l)
{
    for (int d = 0; d < 4; d++) {
        sprite_free(&l->strums[d]);
        sprite_free(&l->splash[d]);
        sprite_free(&l->cover[d]);
    }
    free(l->notes);
    l->notes = NULL;
}

static float note_y(const strumline *l, double t)
{
    float dy = PIXELS_PER_MS * (float)(P.pos - t) * P.scroll_speed;
    return opt.downscroll ? l->y - INITIAL_OFFSET + dy : l->y - INITIAL_OFFSET - dy;
}

static float note_x(const strumline *l, int lane)
{
    float w = P.note_fw[lane] * P.ns.note.scale;
    return l->x + strum_xpos(lane) - (w - STRUMLINE_SIZE) / 2 - NUDGE;
}

/* ------------------------------------------------------------ scoring */

static void popup_spawn(const style_part *p, bool center, float x, float y, float vx, float vy, float ay,
                        float delay)
{
    if (!p->atlas[0]) return;
    popup *pp = NULL;
    for (int i = 0; i < 24; i++)
        if (!P.pops[i].used) {
            pp = &P.pops[i];
            break;
        }
    if (!pp) {
        pp = &P.pops[0];
        sprite_free(&pp->spr);
    }
    memset(pp, 0, sizeof(*pp));
    sprite_init(&pp->spr);
    sprite_load(&pp->spr, p->atlas);
    pp->spr.scale_x = pp->spr.scale_y = p->scale;
    sprite_update_hitbox(&pp->spr);
    pp->spr.x = x + p->off[0] - (center ? pp->spr.width / 2 : 0);
    pp->spr.y = y + p->off[1] - (center ? pp->spr.height / 2 : 0);
    pp->spr.sf_x = pp->spr.sf_y = 0;
    pp->vx = vx;
    pp->vy = vy;
    pp->ay = ay;
    pp->delay = delay;
    pp->life = 0.2f;
    pp->used = true;
}

static void popup_score(int judge, int combo)
{
    double beat = song_beat_length(&P.s, P.pos);
    if (judge >= 0 && judge < 4) {
        popup_spawn(&P.ns.judge[judge], true, GAME_W * 0.474f, GAME_H * 0.45f - 60, -(float)(rand() % 11),
                    -(140 + rand() % 36), 550, (float)(beat * 0.001));
    }
    if (combo >= 10 || combo == 0) {
        int digits[6], nd = 0, c = combo;
        do {
            digits[nd++] = c % 10;
            c /= 10;
        } while (c > 0 && nd < 6);
        while (nd < 3) digits[nd++] = 0;
        for (int i = 0; i < nd; i++) {
            float x = GAME_W * 0.507f - 36 * (i + 1) - 65;
            popup_spawn(&P.ns.num[digits[i]], false, x, GAME_H * 0.44f, (randf() - 0.5f) * 10, -(130 + rand() % 21),
                        250 + rand() % 51, (float)(beat * 0.002));
        }
    }
}

static void apply_score(int score, int judge, float health, bool combo_break)
{
    P.score += score;
    P.health += health;
    if (P.health > HEALTH_MAX) P.health = HEALTH_MAX;
    if (judge >= 0) P.tally[judge]++;
    if (combo_break) {
        if (P.combo >= 10) popup_score(-1, 0);
        P.combo = 0;
        if (judge == 4) P.misses++;
    } else {
        P.combo++;
        if (P.combo > P.max_combo) P.max_combo = P.combo;
    }
}

static int score_pbot1(double ms)
{
    double a = fabs(ms);
    if (a > 160) return MISS_SCORE;
    if (a < 5) return 500;
    double factor = 1.0 - 1.0 / (1.0 + exp(-0.080 * (a - 54.99)));
    return (int)(500 * factor + 9.0);
}

static int judge_pbot1(double ms)
{
    double a = fabs(ms);
    if (a <= 45) return 0;
    if (a <= 90) return 1;
    if (a <= 135) return 2;
    if (a <= 160) return 3;
    return 4;
}

static const char *kind_suffix(const char *kind, bool *noanim)
{
    *noanim = false;
    if (!kind || !kind[0]) return NULL;
    if (!strcmp(kind, "noanim") || !strcmp(kind, "noAnim")) {
        *noanim = true;
        return NULL;
    }
    if (!strcmp(kind, "mom")) return "alt";
    if (!strcmp(kind, "alt")) return "alt";
    return kind;
}

static void play_splash(strumline *l, int d)
{
    sprite *s = &l->splash[d];
    if (s->nanims == 0) return;
    sprite_play_index(s, rand() % s->nanims, true, 0);
    sprite_update_hitbox(s);
    s->visible = true;
    s->sf_x = s->sf_y = 0;
    /* Strumline.playNoteSplash + NoteSplash.play */
    s->x = l->x + strum_xpos(d) + INITIAL_OFFSET + P.ns.splash.off[0] * s->scale_x;
    s->y = l->y - INITIAL_OFFSET + P.ns.splash.off[1] * s->scale_y;
    s->offset_x = s->width * 0.3f;
    s->offset_y = s->height * 0.3f;
}

static void cover_play(strumline *l, int d, int state)
{
    sprite *s = &l->cover[d];
    if (s->nanims == 0) return;
    l->cover_state[d] = state;
    if (state == 0) {
        s->visible = false;
        return;
    }
    sprite_play(s, state == 1 ? "start" : "end", true);
    sprite_update_hitbox(s);
    s->visible = true;
    s->sf_x = s->sf_y = 0;
    if (state != 1) return; /* keeps the place it got at the start */
    /* Strumline.playNoteHoldCover, hard-coded nudges included */
    s->x = l->x + strum_xpos(d) + STRUMLINE_SIZE / 2 - s->width / 2 + P.ns.cover[d].off[0] - 12;
    s->y = l->y + INITIAL_OFFSET + STRUMLINE_SIZE / 2 + P.ns.cover[d].off[1] - 96;
}

static void player_vocals(bool on) { music_set_track_volume(1, on ? 1.0f : 0.0f); }

static void good_hit(note_state *n, double diff)
{
    strumline *l = &P.lines[1];
    int d = n->d->lane;
    int score = score_pbot1(diff);
    int j = judge_pbot1(diff);
    if (j > 3) j = 3;
    static const float hc[4] = {HEALTH_SICK, HEALTH_GOOD, HEALTH_BAD, HEALTH_SHIT};
    n->hit = true;
    P.hit_notes++;
    apply_score(score, j, hc[j], j >= 2);
    popup_score(j, P.combo);
    if (j == 0) play_splash(l, d);
    if (n->d->len > 0) {
        n->holding = true;
        cover_play(l, d, 1);
    }
    strum_play(l, d, "confirm", true);
    player_vocals(true);
    bool noanim;
    const char *suf = kind_suffix(n->d->kind, &noanim);
    if (!noanim) {
        char_sing(&P.bf, d, false, suf);
        P.bf.hold_timer = 0;
    }
    /* GF cheers combos */
    if (P.gf.loaded && (j <= 1)) {
        for (int i = 0; i < P.gf.ncombo; i++)
            if (P.gf.combo_counts[i] == P.combo) {
                char nm[24];
                snprintf(nm, sizeof(nm), "combo%d", P.combo);
                char_play(&P.gf, nm, true, true);
            }
    }
}

static void note_miss(note_state *n)
{
    n->missed = true;
    int prev_combo = P.combo;
    apply_score(MISS_SCORE, 4, HEALTH_MISS, true);
    player_vocals(false);
    sound_play(P.miss_snd[rand() % 3], 0.5f + randf() * 0.1f);
    char_sing(&P.bf, n->d->lane, true, NULL);
    if (P.gf.loaded && P.gf.ndrop) {
        const char *drop = NULL;
        char nm[24];
        for (int i = 0; i < P.gf.ndrop; i++)
            if (prev_combo >= P.gf.drop_counts[i]) {
                snprintf(nm, sizeof(nm), "drop%d", P.gf.drop_counts[i]);
                drop = nm;
            }
        if (drop) char_play(&P.gf, drop, true, true);
    }
}

static void ghost_miss(int d)
{
    P.health += HEALTH_GHOST_MISS;
    P.score += GHOST_MISS_SCORE;
    player_vocals(false);
    sound_play(P.miss_snd[rand() % 3], 0.1f + randf() * 0.1f);
    char_sing(&P.bf, d, true, NULL);
}

/* ------------------------------------------------------------ camera */

static void camera_focus(int who, float dx, float dy, float duration_steps, const char *ease)
{
    float tx = dx, ty = dy;
    character *c = who == 0 ? &P.bf : who == 1 ? &P.dad : who == 2 ? &P.gf : NULL;
    if (who >= 0 && who <= 2) {
        if (!c->loaded) return;
        tx += c->focus_x;
        ty += c->focus_y;
    }
    if (!ease || !strcmp(ease, "CLASSIC")) {
        P.tw_active = false;
        P.follow_x = tx;
        P.follow_y = ty;
        return;
    }
    float dur = (float)(song_step_length(&P.s, P.pos) * duration_steps / 1000.0);
    if (!strcmp(ease, "INSTANT") || dur <= 0) {
        P.tw_active = false;
        P.follow_x = tx;
        P.follow_y = ty;
        P.cam_x = tx - GAME_W / 2;
        P.cam_y = ty - GAME_H / 2;
        return;
    }
    P.tw_active = true;
    P.tw_from_x = P.cam_x + GAME_W / 2;
    P.tw_from_y = P.cam_y + GAME_H / 2;
    P.tw_to_x = tx;
    P.tw_to_y = ty;
    P.follow_x = tx;
    P.follow_y = ty;
    P.tw_t = 0;
    P.tw_dur = dur;
    strncpy(P.tw_ease, ease, sizeof(P.tw_ease) - 1);
}

static void camera_zoom(float zoom, float duration_steps, const char *ease, bool direct)
{
    float target = zoom * (direct ? 1.0f : P.stage_zoom);
    float dur = (float)(song_step_length(&P.s, P.pos) * duration_steps / 1000.0);
    if (!ease || !strcmp(ease, "INSTANT") || dur <= 0) {
        P.zoom_active = false;
        P.cur_zoom = target;
        return;
    }
    P.zoom_active = true;
    P.zoom_from = P.cur_zoom;
    P.zoom_to = target;
    P.zoom_t = 0;
    P.zoom_dur = dur;
    strncpy(P.zoom_ease, ease, sizeof(P.zoom_ease) - 1);
}

static void ease_name(char *out, size_t n, const cJSON *v, const char *def_ease)
{
    const char *e = jstr(v, "ease", def_ease);
    const char *dir = jstr(v, "easeDir", "");
    if (!strcmp(e, "linear") || !strcmp(e, "INSTANT") || !strcmp(e, "CLASSIC") || strstr(e, "In") || strstr(e, "Out"))
        dir = "";
    snprintf(out, n, "%s%s", e, dir);
}

static character *target_char(const char *t)
{
    if (!t) return NULL;
    if (!strcmp(t, "bf") || !strcmp(t, "boyfriend") || !strcmp(t, "player")) return &P.bf;
    if (!strcmp(t, "dad") || !strcmp(t, "opponent")) return &P.dad;
    if (!strcmp(t, "gf") || !strcmp(t, "girlfriend")) return &P.gf;
    return NULL;
}

static void run_event(const song_event *e)
{
    const cJSON *v = e->value;
    char ease[32];
    if (!strcmp(e->kind, "FocusCamera")) {
        int who = cJSON_IsNumber(v) ? (int)v->valuedouble : (int)jnum(v, "char", 0);
        ease_name(ease, sizeof(ease), v, "CLASSIC");
        camera_focus(who, jnum(v, "x", 0), jnum(v, "y", 0), jnum(v, "duration", 4), ease);
    } else if (!strcmp(e->kind, "ZoomCamera")) {
        if (!opt.camera_zoom) return;
        ease_name(ease, sizeof(ease), v, "linear");
        double z = cJSON_IsNumber(v) ? v->valuedouble : jnum(v, "zoom", 1);
        camera_zoom(z, jnum(v, "duration", 4), ease, !strcmp(jstr(v, "mode", "stage"), "direct"));
    } else if (!strcmp(e->kind, "SetCameraBop")) {
        P.zoom_rate = (int)jnum(v, "rate", 4);
        P.bop_intensity = jnum(v, "intensity", 1.015);
        P.hud_bop_intensity = (P.bop_intensity - 1.0f) * 2.0f;
        P.zoom_rate_offset = (int)jnum(v, "offset", 0);
    } else if (!strcmp(e->kind, "PlayAnimation")) {
        const char *t = jstr(v, "target", "bf");
        const char *anim = jstr(v, "anim", "idle");
        bool force = jbool(v, "force", false);
        character *c = target_char(t);
        if (c) char_play(c, anim, force, force);
        else {
            stage_prop *p = stage_find_prop(&P.st, t);
            if (p) sprite_play(&p->spr, anim, force);
        }
    } else if (!strcmp(e->kind, "ScrollSpeed")) {
        float sc = jnum(v, "scroll", 1);
        bool absolute = jbool(v, "absolute", false);
        float target = absolute ? sc : (float)P.s.scroll_speed * sc;
        float dur = (float)(song_step_length(&P.s, P.pos) * jnum(v, "duration", 4) / 1000.0);
        ease_name(ease, sizeof(ease), v, "linear");
        if (!strcmp(ease, "INSTANT") || dur <= 0) {
            P.scroll_speed = target;
            P.scroll_dur = 0;
        } else {
            P.scroll_from = P.scroll_speed;
            P.scroll_target = target;
            P.scroll_t = 0;
            P.scroll_dur = dur;
        }
    }
}

/* ------------------------------------------------------------ setup */

static void draw_loading_screen(const char *song_name)
{
    gfx_begin();
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, 255));
    text_draw("Loading...", GAME_W - 30, GAME_H - 60, 32, 0xffffffff, ALIGN_RIGHT);
    text_draw(song_name, GAME_W - 30, GAME_H - 100, 24, 0xbbbbbbff, ALIGN_RIGHT);
    gfx_end();
}

static void start_music(void)
{
    const char *paths[3] = {NULL, NULL, NULL};
    static char p0[256], p1[256], p2[256];
    if (P.s.inst_path[0]) paths[0] = strcpy(p0, fs_path(P.s.inst_path));
    if (P.s.player_voice[0]) paths[1] = strcpy(p1, fs_path(P.s.player_voice));
    if (P.s.opp_voice[0]) paths[2] = strcpy(p2, fs_path(P.s.opp_voice));
    music_open(paths, 3, false);
    music_set_volume(1);
    music_play();
}

static void reset_song_state(void)
{
    P.health = P.health_lerp = HEALTH_MAX / 2;
    P.score = P.combo = P.max_combo = P.misses = 0;
    memset(P.tally, 0, sizeof(P.tally));
    P.hit_notes = 0;
    P.started = P.ending = P.finished = false;
    P.paused = P.dead = P.results = false;
    P.next_event = 0;
    P.scroll_speed = P.s.scroll_speed > 0 ? P.s.scroll_speed : 1;
    P.scroll_dur = 0;
    P.pos = -song_beat_length(&P.s, 0) * 5;
    P.last_step = (int)floor(song_step_at(&P.s, P.pos)) - 1;
    P.last_beat = P.last_step / 4;
    P.count_step = -1;
    for (int i = 0; i < 4; i++) P.count_alpha[i] = 0;
    for (int i = 0; i < 2; i++) {
        strumline *l = &P.lines[i];
        for (int k = 0; k < l->n; k++) {
            l->notes[k].hit = l->notes[k].missed = l->notes[k].holding = false;
            l->notes[k].hold_done = l->notes[k].hold_dropped = false;
        }
        l->first = 0;
        for (int d = 0; d < 4; d++) {
            strum_play(l, d, "static", true);
            l->splash[d].visible = false;
            l->cover[d].visible = false;
            l->cover_state[d] = 0;
            l->confirm_timer[d] = 0;
        }
    }
    for (int i = 0; i < 24; i++)
        if (P.pops[i].used) {
            sprite_free(&P.pops[i].spr);
            P.pops[i].used = false;
        }
    P.stage_zoom = P.st.zoom > 0 ? P.st.zoom : 1;
    P.cur_zoom = P.stage_zoom;
    P.bop_mult = 1;
    P.hud.zoom = 1;
    P.zoom_active = P.tw_active = false;
    P.zoom_rate = 4;
    P.zoom_rate_offset = 0;
    P.bop_intensity = 1.015f;
    P.hud_bop_intensity = 0.03f;
    if (P.bf.loaded) {
        P.bf.dead = false;
        P.bf.locked = false;
        stage_place(&P.st, &P.bf);
    }
    if (P.dad.loaded) stage_place(&P.st, &P.dad);
    if (P.gf.loaded) stage_place(&P.st, &P.gf);
    /* the camera starts on the opponent (after placing, which sets the focus points) */
    character *c = P.dad.loaded ? &P.dad : &P.bf;
    P.follow_x = c->focus_x;
    P.follow_y = c->focus_y;
    P.cam_x = P.follow_x - GAME_W / 2;
    P.cam_y = P.follow_y - GAME_H / 2;
    music_close();
}

static bool load_song(void)
{
    const char *id = playreq.songs[playreq.index];
    draw_loading_screen(id);
    if (!song_load(&P.s, id, playreq.variation, playreq.difficulty)) {
        LOG("song load failed %s %s %s", id, playreq.variation, playreq.difficulty);
        return false;
    }
    stage_load(&P.st, P.s.stage);
    P.pixel = !strcmp(P.s.notestyle, "pixel");
    style_load(&P.ns, P.s.notestyle);
    if (strcmp(P.s.girlfriend, "") && strcmp(P.s.girlfriend, "none")) char_load(&P.gf, P.s.girlfriend, CT_GF);
    char_load(&P.dad, P.s.opponent, CT_DAD);
    char_load(&P.bf, P.s.player, CT_BF);

    /* notes per side */
    for (int i = 0; i < 2; i++) {
        strumline *l = &P.lines[i];
        line_init(l, i == 1);
        int n = 0;
        for (int k = 0; k < P.s.nnotes; k++)
            if (P.s.notes[k].player == (i == 1)) n++;
        l->notes = calloc(n > 0 ? n : 1, sizeof(note_state));
        for (int k = 0; k < P.s.nnotes; k++)
            if (P.s.notes[k].player == (i == 1)) l->notes[l->n++].d = &P.s.notes[k];
    }
    P.total_notes = P.lines[1].n;
    P.note_atlas = atlas_get(P.ns.note.atlas);
    for (int d = 0; d < 4; d++) {
        P.note_frame[d] = -1;
        const char *key = part_key(&P.ns.note, dir_names[d]);
        const atl_anim *an = atlas_anim(P.note_atlas, key);
        if (an) {
            P.note_frame[d] = an->frames[0];
            P.note_fw[d] = P.note_atlas->frames[an->frames[0]].fw;
            P.note_fh[d] = P.note_atlas->frames[an->frames[0]].fh;
        }
    }
    P.hold_atlas = atlas_get(P.ns.hold.atlas);

    float sx_opp = STRUMLINE_X_OFFSET, sx_pl = GAME_W / 2 + STRUMLINE_X_OFFSET;
    if (opt.middlescroll) sx_pl = (GAME_W - 4 * NOTE_SPACING) / 2 + STRUMLINE_X_OFFSET;
    float h = P.lines[0].strum_h[0];
    float sy = opt.downscroll ? GAME_H - h - STRUMLINE_Y_OFFSET - P.ns.strum.off[1] : STRUMLINE_Y_OFFSET;
    line_place(&P.lines[0], sx_opp, sy);
    line_place(&P.lines[1], sx_pl, sy);
    P.lines[0].visible = !opt.middlescroll;

    /* HUD */
    cJSON *ui = fs_json("ui.json");
    sprite_init(&P.health_bg);
    sprite_load(&P.health_bg, jstr(cJSON_GetObjectItem(ui, "healthBar"), "atlas", "img/shared/healthBar"));
    sprite_update_hitbox(&P.health_bg);
    P.health_bg.y = opt.downscroll ? GAME_H * 0.1f : GAME_H * 0.9f;
    sprite_screen_center(&P.health_bg, true, false);
    P.health_bg.sf_x = P.health_bg.sf_y = 0;
    cJSON_Delete(ui);
    icon_load(&P.icon_p1, &P.bf, true, &P.icon_size1);
    icon_load(&P.icon_p2, &P.dad, false, &P.icon_size2);

    for (int i = 0; i < 3; i++) {
        char p[96];
        snprintf(p, sizeof(p), "snd/shared/sounds/missnote%d.ogg", i + 1);
        P.miss_snd[i] = sound_load(fs_path(p));
    }
    for (int i = 0; i < 4; i++) {
        if (P.ns.count[i].sound[0]) P.count_snd[i] = sound_load(fs_path(P.ns.count[i].sound));
        sprite_init(&P.count_spr[i]);
        if (P.ns.count[i].atlas[0]) {
            sprite_load(&P.count_spr[i], P.ns.count[i].atlas);
            P.count_spr[i].scale_x = P.count_spr[i].scale_y = P.ns.count[i].scale;
            sprite_update_hitbox(&P.count_spr[i]);
            sprite_screen_center(&P.count_spr[i], true, true);
            P.count_spr[i].sf_x = P.count_spr[i].sf_y = 0;
        }
    }
    P.death_snd = sound_load(fs_path(P.pixel ? "snd/shared/sounds/gameplay/gameover/fnf_loss_sfx-pixel.ogg"
                                             : "snd/shared/sounds/gameplay/gameover/fnf_loss_sfx.ogg"));
    P.loaded = true;
    {
        struct mallinfo mi = mallinfo();
        u32 a1 = (u32)SYS_GetArena1Hi() - (u32)SYS_GetArena1Lo(), a2 = (u32)SYS_GetArena2Hi() - (u32)SYS_GetArena2Lo();
        LOG("loaded %s: %d notes, textures %u KB, heap free %u KB, arenas left %u+%u KB", id, P.s.nnotes,
            (unsigned)(atlas_memory() / 1024), (unsigned)(mi.fordblks / 1024), (unsigned)(a1 / 1024), (unsigned)(a2 / 1024));
    }
    autotest_log_state("song %s notes %d tex %uKB", id, P.s.nnotes, (unsigned)(atlas_memory() / 1024));
    reset_song_state();
    return true;
}

static void unload_song(void)
{
    if (!P.loaded) return;
    music_close();
    sound_stop_all();
    for (int i = 0; i < 2; i++) line_free(&P.lines[i]);
    char_free(&P.bf);
    char_free(&P.dad);
    char_free(&P.gf);
    stage_free(&P.st);
    atlas_put(P.note_atlas);
    atlas_put(P.hold_atlas);
    sprite_free(&P.health_bg);
    sprite_free(&P.icon_p1);
    sprite_free(&P.icon_p2);
    for (int i = 0; i < 4; i++) {
        sprite_free(&P.count_spr[i]);
        sound_free(P.count_snd[i]);
        P.count_snd[i] = NULL;
    }
    for (int i = 0; i < 3; i++) {
        sound_free(P.miss_snd[i]);
        P.miss_snd[i] = NULL;
    }
    sound_free(P.death_snd);
    P.death_snd = NULL;
    for (int i = 0; i < 24; i++)
        if (P.pops[i].used) sprite_free(&P.pops[i].spr);
    song_free(&P.s);
    memset(&P, 0, sizeof(P));
    atlas_purge();
}

void play_enter(void)
{
    menu_music_stop();
    memset(&P, 0, sizeof(P));
    if (!load_song()) {
        unload_song();
        game_switch(playreq.story ? SCENE_STORY : SCENE_FREEPLAY);
    }
}

void play_exit(void)
{
    unload_song();
}

/* ------------------------------------------------------------ update */

static void on_step(int step)
{
    char_step(&P.bf, step);
    char_step(&P.dad, step);
    char_step(&P.gf, step);
    stage_step(&P.st, step);
    if (step % 4 == 0) {
        P.icon_size1 = 1.2f;
        P.icon_size2 = 1.2f;
    }
    if (opt.camera_zoom && P.started && P.zoom_rate > 0 && P.hud.zoom < 1.35f &&
        (step + P.zoom_rate_offset * 4) % (P.zoom_rate * 4) == 0) {
        P.bop_mult = P.bop_intensity;
        P.hud.zoom += P.hud_bop_intensity;
    }
}

static void countdown_step(int k)
{
    /* k: 0 three, 1 two (ready), 2 one (set), 3 go */
    if (k < 0 || k > 3) return;
    if (P.count_snd[k]) sound_play(P.count_snd[k], 0.6f);
    if (P.count_spr[k].base) P.count_alpha[k] = 1;
}

static void update_notes(float dt)
{
    /* opponent: plays itself */
    strumline *o = &P.lines[0];
    for (int k = o->first; k < o->n; k++) {
        note_state *n = &o->notes[k];
        if (n->d->t > P.pos) break;
        if (!n->hit) {
            n->hit = true;
            bool noanim;
            const char *suf = kind_suffix(n->d->kind, &noanim);
            if (!noanim) {
                char_sing(&P.dad, n->d->lane, false, suf);
                P.dad.hold_timer = 0;
            }
            strum_play(o, n->d->lane, "confirm", true);
            o->confirm_timer[n->d->lane] = 0;
            music_set_track_volume(2, 1);
            if (n->d->len > 0) n->holding = true;
        }
        if (n->holding) {
            if (P.pos >= n->d->t + n->d->len) {
                n->holding = false;
                n->hold_done = true;
            } else {
                P.dad.hold_timer = 0;
                if (!strcmp(sprite_anim_name(&o->strums[n->d->lane]), "confirm") &&
                    sprite_anim_finished(&o->strums[n->d->lane]))
                    strum_play(o, n->d->lane, "confirm-hold", false);
                o->confirm_timer[n->d->lane] = 0;
            }
        }
    }
    while (o->first < o->n && o->notes[o->first].hit && !o->notes[o->first].holding &&
           o->notes[o->first].d->t + o->notes[o->first].d->len < P.pos - 500)
        o->first++;
    for (int d = 0; d < 4; d++) {
        sprite *s = &o->strums[d];
        const char *a = sprite_anim_name(s);
        if (!strncmp(a, "confirm", 7)) {
            o->confirm_timer[d] += dt;
            bool holding = false;
            for (int k = o->first; k < o->n; k++)
                if (o->notes[k].holding && o->notes[k].d->lane == d) holding = true;
            if (!holding && sprite_anim_finished(s) && o->confirm_timer[d] > 0.15f) strum_play(o, d, "static", true);
        }
    }

    /* player */
    strumline *l = &P.lines[1];
    u8 held = in_lanes_held(), pressed = in_lanes_pressed(), released = in_lanes_released();
    if (opt.botplay) {
        pressed = released = 0;
        held = 0;
        for (int k = l->first; k < l->n; k++) {
            note_state *n = &l->notes[k];
            if (n->d->t > P.pos + 20) break;
            if (n->holding) held |= 1 << n->d->lane;
        }
    }
    double window_start = P.pos - HIT_WINDOW;
    /* misses: notes that passed the window */
    for (int k = l->first; k < l->n; k++) {
        note_state *n = &l->notes[k];
        if (n->d->t > window_start) break;
        if (!n->hit && !n->missed) note_miss(n);
    }
    /* presses */
    for (int d = 0; d < 4; d++) {
        bool bot_hit = false;
        if (opt.botplay) {
            for (int k = l->first; k < l->n; k++) {
                note_state *n = &l->notes[k];
                if (n->d->t > P.pos) break;
                if (!n->hit && !n->missed && n->d->lane == d) {
                    good_hit(n, 0);
                    bot_hit = true;
                }
            }
            if (bot_hit) l->pressed[d] = true;
            continue;
        }
        if (pressed & (1 << d)) {
            l->pressed[d] = true;
            P.bf.hold_timer = 0;
            note_state *best = NULL;
            bool any_in_range = false;
            for (int k = l->first; k < l->n; k++) {
                note_state *n = &l->notes[k];
                if (n->d->t > P.pos + HIT_WINDOW) break;
                if (n->hit || n->missed || n->d->t < window_start) continue;
                any_in_range = true;
                if (n->d->lane == d && !best) best = n;
            }
            if (best) {
                good_hit(best, P.pos - best->d->t);
            } else {
                bool may_ghost = opt.ghost_tapping && !any_in_range;
                if (!may_ghost) ghost_miss(d);
                strum_play(l, d, "press", true);
            }
        }
        if (released & (1 << d)) {
            l->pressed[d] = false;
            strum_play(l, d, "static", true);
        }
    }
    /* holds */
    for (int k = l->first; k < l->n; k++) {
        note_state *n = &l->notes[k];
        if (n->d->t > P.pos) break;
        if (!n->holding) continue;
        int d = n->d->lane;
        double end = n->d->t + n->d->len;
        if (P.pos >= end) {
            n->holding = false;
            n->hold_done = true;
            if (l->cover_state[d] == 1) cover_play(l, d, 2);
            if (opt.botplay) strum_play(l, d, "static", true);
            continue;
        }
        if (opt.botplay || (held & (1 << d))) {
            P.health += HEALTH_HOLD_PER_SEC * dt;
            if (P.health > HEALTH_MAX) P.health = HEALTH_MAX;
            P.score += (int)(HOLD_SCORE_PER_SEC * dt + 0.5f);
            P.bf.hold_timer = 0;
            sprite *s = &l->strums[d];
            if (!strcmp(sprite_anim_name(s), "confirm") && sprite_anim_finished(s)) strum_play(l, d, "confirm-hold", false);
            else if (strncmp(sprite_anim_name(s), "confirm", 7)) strum_play(l, d, "confirm", false);
        } else {
            n->holding = false;
            cover_play(l, d, 0);
            if (end - P.pos > HOLD_DROP_THRESHOLD) {
                n->hold_dropped = true;
                apply_score(0, -1, 0, true);
                player_vocals(false);
                char_sing(&P.bf, d, true, NULL);
            } else {
                n->hold_done = true;
            }
        }
    }
    while (l->first < l->n) {
        note_state *n = &l->notes[l->first];
        if ((n->hit || n->missed) && !n->holding && n->d->t + n->d->len < P.pos - 1000) l->first++;
        else break;
    }
    /* the bot lets go of a tapped note once its confirm animation is over */
    if (opt.botplay) {
        for (int d = 0; d < 4; d++) {
            bool holding = false;
            for (int k = l->first; k < l->n; k++)
                if (l->notes[k].holding && l->notes[k].d->lane == d) holding = true;
            sprite *s = &l->strums[d];
            if (!holding && !strcmp(sprite_anim_name(s), "confirm") && sprite_anim_finished(s)) {
                strum_play(l, d, "static", true);
                l->pressed[d] = false;
            }
        }
    }
    /* covers and splashes */
    for (int d = 0; d < 4; d++) {
        sprite *c = &l->cover[d];
        if (c->visible) {
            sprite_update(c, dt);
            if (l->cover_state[d] == 1 && !strcmp(sprite_anim_name(c), "start") && sprite_anim_finished(c))
                sprite_play(c, "hold", true);
            if (l->cover_state[d] == 2 && sprite_anim_finished(c)) cover_play(l, d, 0);
        }
        if (l->splash[d].visible) {
            sprite_update(&l->splash[d], dt);
            if (sprite_anim_finished(&l->splash[d])) l->splash[d].visible = false;
        }
    }
}

static void finish_song(void)
{
    if (P.ending) return;
    P.ending = true;
    float acc = P.total_notes ? (P.tally[0] + P.tally[1]) / (float)P.total_notes : 0;
    if (!opt.botplay) save_set_score(P.s.id, playreq.difficulty, playreq.variation, P.score, acc);
    if (playreq.story) {
        playreq.week_score += P.score;
        playreq.week_misses += P.misses;
        if (playreq.index + 1 < playreq.nsongs) {
            playreq.index++;
            unload_song();
            if (!load_song()) game_switch(SCENE_STORY);
            return;
        }
        if (!opt.botplay) save_set_week_score(playreq.week, playreq.difficulty, playreq.week_score);
    }
    P.results = true;
    P.results_t = 0;
    music_close();
    autotest_log_state("results score %d sick %d good %d bad %d shit %d miss %d", P.score, P.tally[0], P.tally[1],
                       P.tally[2], P.tally[3], P.tally[4]);
}

static void start_death(void)
{
    P.dead = true;
    P.death_stage = 0;
    P.death_timer = 0;
    music_close();
    sound_play(P.death_snd, 1);
    P.bf.dead = false;
    P.bf.locked = false;
    char_play(&P.bf, "firstDeath", true, false);
    P.bf.dead = true;
    P.follow_x = P.bf.focus_x + P.bf.death_cam_off[0];
    P.follow_y = P.bf.focus_y + P.bf.death_cam_off[1];
    P.tw_active = false;
    P.zoom_active = false;
    P.cur_zoom = P.stage_zoom * P.bf.death_zoom;
    autotest_log_state("game over");
}

static void restart_song(void)
{
    reset_song_state();
}

static void update_death(float dt)
{
    P.death_timer += dt;
    sprite_update(&P.bf.spr, dt);
    if (P.death_stage == 0 && sprite_anim_finished(&P.bf.spr)) {
        P.death_stage = 1;
        P.bf.dead = false;
        char_play(&P.bf, "deathLoop", true, false);
        P.bf.dead = true;
        const char *p[1] = {fs_path(P.pixel ? "snd/shared/music/gameplay/gameover/gameOver-pixel.ogg"
                                            : "snd/shared/music/gameplay/gameover/gameOver.ogg")};
        music_open(p, 1, true);
        music_play();
    }
    u32 pr = in_pressed();
    if (P.death_stage < 2 && (pr & ACT_ACCEPT)) {
        P.death_stage = 2;
        P.death_timer = 0;
        P.bf.dead = false;
        char_play(&P.bf, "deathConfirm", true, false);
        P.bf.dead = true;
        const char *p[1] = {fs_path(P.pixel ? "snd/shared/music/gameplay/gameover/gameOverEnd-pixel.ogg"
                                            : "snd/shared/music/gameplay/gameover/gameOverEnd.ogg")};
        music_open(p, 1, false);
        music_play();
    } else if (P.death_stage < 2 && (pr & ACT_BACK)) {
        music_close();
        game_switch(playreq.story ? SCENE_STORY : SCENE_FREEPLAY);
    }
    if (P.death_stage == 2 && P.death_timer > 2.5f) restart_song();
}

static const char *const pause_items[] = {"Resume", "Restart Song", "Botplay", "Exit to menu"};
#define NPAUSE 4

static void update_pause(float dt)
{
    P.pause_alpha = fminf(0.6f, P.pause_alpha + dt * 1.5f);
    u32 nav = in_nav(), pr = in_pressed();
    if (nav & ACT_UP) {
        P.pause_sel = (P.pause_sel + NPAUSE - 1) % NPAUSE;
        sfx_scroll();
    }
    if (nav & ACT_DOWN) {
        P.pause_sel = (P.pause_sel + 1) % NPAUSE;
        sfx_scroll();
    }
    if (pr & ACT_BACK) {
        P.paused = false;
        if (P.started) music_play();
        return;
    }
    if (pr & ACT_ACCEPT) {
        switch (P.pause_sel) {
        case 0:
            P.paused = false;
            if (P.started) music_play();
            break;
        case 1:
            P.paused = false;
            restart_song();
            break;
        case 2:
            opt.botplay = !opt.botplay;
            sfx_confirm();
            break;
        case 3:
            P.paused = false;
            music_close();
            game_switch(playreq.story ? SCENE_STORY : SCENE_FREEPLAY);
            break;
        }
    }
}

static void update_camera(float dt)
{
    float k60 = dt * 60;
    if (P.tw_active) {
        P.tw_t += dt;
        float t = ease_apply(P.tw_ease, P.tw_dur > 0 ? P.tw_t / P.tw_dur : 1);
        float x = lerpf(P.tw_from_x, P.tw_to_x, t), y = lerpf(P.tw_from_y, P.tw_to_y, t);
        P.cam_x = x - GAME_W / 2;
        P.cam_y = y - GAME_H / 2;
        if (P.tw_t >= P.tw_dur) P.tw_active = false;
    } else {
        float lerp = 1 - powf(1 - 0.04f, k60);
        P.cam_x += (P.follow_x - GAME_W / 2 - P.cam_x) * lerp;
        P.cam_y += (P.follow_y - GAME_H / 2 - P.cam_y) * lerp;
    }
    if (P.zoom_active) {
        P.zoom_t += dt;
        float t = ease_apply(P.zoom_ease, P.zoom_dur > 0 ? P.zoom_t / P.zoom_dur : 1);
        P.cur_zoom = lerpf(P.zoom_from, P.zoom_to, t);
        if (P.zoom_t >= P.zoom_dur) P.zoom_active = false;
    }
    float decay = powf(0.95f, k60);
    P.bop_mult = lerpf(1.0f, P.bop_mult, decay);
    P.hud.zoom = lerpf(1.0f, P.hud.zoom, decay);
    P.cam.zoom = P.cur_zoom * P.bop_mult;
    if (P.scroll_dur > 0) {
        P.scroll_t += dt;
        float t = ease_apply("linear", P.scroll_t / P.scroll_dur);
        P.scroll_speed = lerpf(P.scroll_from, P.scroll_target, t);
        if (P.scroll_t >= P.scroll_dur) P.scroll_dur = 0;
    }
}

bool play_debug_seek(double ms)
{
    if (!P.loaded || !P.started || P.results) return false;
    music_seek_ms(ms);
    return true;
}

void play_update(float dt)
{
    if (!P.loaded) return;
    P.time += dt;
    {
        static int dbg;
        if (dbg++ == 90) {
            character *cs[3] = {&P.bf, &P.dad, &P.gf};
            for (int i = 0; i < 3; i++)
                LOG("char %s pos %.0f,%.0f size %.0fx%.0f focus %.0f,%.0f z %d", cs[i]->id, cs[i]->spr.x, cs[i]->spr.y,
                    cs[i]->spr.width, cs[i]->spr.height, cs[i]->focus_x, cs[i]->focus_y, cs[i]->z);
            LOG("cam %.0f,%.0f follow %.0f,%.0f zoom %.2f tw %d", P.cam_x, P.cam_y, P.follow_x, P.follow_y, P.cam.zoom, P.tw_active);
        }
    }
    if (P.results) {
        P.results_t += dt;
        if (P.results_t > 0.5f && (in_pressed() & (ACT_ACCEPT | ACT_BACK))) {
            sfx_confirm();
            game_switch(playreq.story ? SCENE_STORY : SCENE_FREEPLAY);
        }
        return;
    }
    if (P.dead) {
        update_death(dt);
        update_camera(dt);
        return;
    }
    if (P.paused) {
        update_pause(dt);
        return;
    }
    if (in_pressed() & ACT_PAUSE) {
        P.paused = true;
        P.pause_sel = 0;
        P.pause_alpha = 0;
        music_pause();
        return;
    }
    if ((in_pressed() & ACT_RESET) && !opt.botplay) {
        P.health = 0;
    }

    /* conductor */
    if (!P.started) {
        P.pos += dt * 1000;
        int beat = (int)floor(song_beat_at(&P.s, P.pos));
        int k = beat + 4; /* beats -4..-1: three, two, one, go */
        if (k != P.count_step && k >= 0 && k <= 3) countdown_step(k);
        P.count_step = k;
        if (P.pos >= 0) {
            P.started = true;
            start_music();
            P.pos = 0;
        }
    } else {
        double t = music_time_ms();
        P.pos = t + P.s.inst_offset - opt.offset_ms;
        if (music_finished() || (music_length_ms() > 0 && t >= music_length_ms() - 5)) {
            /* let the last notes play out */
            finish_song();
            if (P.results || !P.loaded) return;
        }
    }
    int step = (int)floor(song_step_at(&P.s, P.pos));
    while (P.last_step < step) {
        P.last_step++;
        on_step(P.last_step);
    }
    while (P.next_event < P.s.nevents && P.s.events[P.next_event].t <= P.pos) {
        run_event(&P.s.events[P.next_event]);
        P.next_event++;
    }

    update_notes(dt);

    double step_len = song_step_length(&P.s, P.pos);
    bool holding = in_lanes_held() != 0;
    if (opt.botplay) {
        holding = false;
        for (int k = P.lines[1].first; k < P.lines[1].n; k++)
            if (P.lines[1].notes[k].holding) holding = true;
    }
    char_update(&P.bf, dt, step_len, holding);
    char_update(&P.dad, dt, step_len, false);
    char_update(&P.gf, dt, step_len, false);
    stage_update(&P.st, dt);
    for (int i = 0; i < 2; i++)
        for (int d = 0; d < 4; d++) sprite_update(&P.lines[i].strums[d], dt);

    for (int i = 0; i < 4; i++) {
        if (P.count_alpha[i] > 0) {
            P.count_alpha[i] -= dt / (float)(song_beat_length(&P.s, 0) / 1000.0);
            if (P.count_alpha[i] < 0) P.count_alpha[i] = 0;
        }
    }
    for (int i = 0; i < 24; i++) {
        popup *p = &P.pops[i];
        if (!p->used) continue;
        p->vy += p->ay * dt;
        p->spr.x += p->vx * dt;
        p->spr.y += p->vy * dt;
        if (p->delay > 0) p->delay -= dt;
        else {
            p->life -= dt;
            p->spr.alpha = p->life / 0.2f;
            if (p->life <= 0) {
                sprite_free(&p->spr);
                p->used = false;
            }
        }
    }
    P.health_lerp = lerpf(P.health_lerp, P.health, 1 - powf(0.85f, dt * 60));
    P.icon_size1 = lerpf(1.0f, P.icon_size1, powf(0.85f, dt * 60));
    P.icon_size2 = lerpf(1.0f, P.icon_size2, powf(0.85f, dt * 60));
    update_camera(dt);

    if (P.health <= 0 && !opt.botplay && !P.ending) start_death();
}

/* ------------------------------------------------------------ drawing */

static void draw_region(atlas *a, int frame, float fx0, float fy0, float fx1, float fy1, float x, float y, float w,
                        float h, u32 color)
{
    /* Draws the part (fractions fx0..fx1, fy0..fy1) of a one-piece frame to the rectangle x,y,w,h. */
    if (!a || frame < 0 || frame >= a->nframes) return;
    const atl_frame *f = &a->frames[frame];
    if (!f->count) return;
    const atl_piece *q = &a->pieces[f->first];
    texture *t = &a->pages[q->page];
    float u0 = (q->u + q->uw * fx0) / t->w, u1 = (q->u + q->uw * fx1) / t->w;
    float v0 = (q->v + q->vh * fy0) / t->h, v1 = (q->v + q->vh * fy1) / t->h;
    float pts[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
    float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
    gfx_draw_pts(t, pts, uv, color);
}

static void draw_hold(const strumline *l, const note_state *n)
{
    if (!P.hold_atlas || P.hold_atlas->nframes == 0) return;
    const atl_frame *f = &P.hold_atlas->frames[0];
    float zoom = P.ns.hold.scale;
    float gw = f->fw / 8 * zoom;           /* one column */
    float gh_tex = f->fh * zoom;           /* texture height on screen */
    int d = n->d->lane;
    float end_offset = P.pixel ? 1.0f : 0.5f, bottom_clip = P.pixel ? 1.0f : 0.9f;
    double start_t = n->d->t;
    double remaining = n->d->len;
    if (n->hit && (n->holding || n->hold_done)) {
        remaining = n->d->t + n->d->len - P.pos;
        start_t = P.pos;
    }
    if (remaining <= 0) return;
    float len_px = (float)(remaining * PIXELS_PER_MS * P.scroll_speed);
    float x = l->x + strum_xpos(d) + STRUMLINE_SIZE / 2 - gw / 2 + P.ns.hold.off[0];
    float y0 = note_y(l, start_t) + STRUMLINE_SIZE / 2;
    float dir = opt.downscroll ? -1 : 1;
    u32 col = rgba(255, 255, 255, n->hold_dropped || n->missed ? 150 : 255);
    float body_u0 = (d * 2) / 8.0f, body_u1 = (d * 2 + 1) / 8.0f;
    float cap_u0 = body_u1, cap_u1 = (d * 2 + 2) / 8.0f;
    /* cap: texture v 0..bottom_clip spans [len - gh*end_offset, len + gh*(bottom_clip-end_offset)] */
    float cap_top = len_px - gh_tex * end_offset;
    float body_end = cap_top > 0 ? cap_top : 0;
    /* body, tiled by the texture height */
    float yy = 0;
    while (yy < body_end) {
        float seg = fminf(gh_tex, body_end - yy);
        float v1 = seg / gh_tex;
        float ys = y0 + dir * yy;
        if (opt.downscroll) draw_region(P.hold_atlas, 0, body_u0, 0, body_u1, v1, x, ys - seg, gw, seg, col);
        else draw_region(P.hold_atlas, 0, body_u0, 0, body_u1, v1, x, ys, gw, seg, col);
        yy += seg;
    }
    float c0 = cap_top < 0 ? -cap_top : 0; /* clipped part of the cap */
    float cap_len = gh_tex * bottom_clip - c0;
    if (cap_len > 0) {
        float v0 = c0 / gh_tex, v1 = bottom_clip;
        float ys = y0 + dir * (cap_top + c0);
        if (opt.downscroll) draw_region(P.hold_atlas, 0, cap_u0, v1, cap_u1, v0, x, ys - cap_len, gw, cap_len, col);
        else draw_region(P.hold_atlas, 0, cap_u0, v0, cap_u1, v1, x, ys, gw, cap_len, col);
    }
}

static void draw_line(const strumline *l)
{
    if (!l->visible) return;
    for (int d = 0; d < 4; d++) sprite_draw(&l->strums[d], 0, 0);
    float top = -200, bottom = GAME_H + 200;
    for (int k = l->first; k < l->n; k++) {
        const note_state *n = &l->notes[k];
        if (n->d->len > 0 && !n->hold_done) {
            float y = note_y(l, n->d->t);
            if ((opt.downscroll ? y < top : y > bottom)) break;
            if (!n->missed || n->d->t + n->d->len > P.pos) draw_hold(l, n);
        }
    }
    for (int k = l->first; k < l->n; k++) {
        const note_state *n = &l->notes[k];
        float y = note_y(l, n->d->t);
        if (opt.downscroll ? y < top : y > bottom) break;
        if (n->hit) continue;
        int d = n->d->lane;
        if (P.note_frame[d] < 0) continue;
        float sc = P.ns.note.scale;
        float m[6] = {sc, 0, 0, sc, note_x(l, d), y};
        atlas_draw_frame(P.note_atlas, P.note_frame[d], m, n->missed ? rgba(255, 255, 255, 110) : 0xffffffff);
    }
    for (int d = 0; d < 4; d++) {
        sprite_draw(&l->cover[d], 0, 0);
        sprite_draw(&l->splash[d], 0, 0);
    }
}

static void draw_icon(sprite *s, float size, bool player, float bar_x, float bar_w, float bar_y)
{
    if (!s->base) return;
    int frames = s->base->nframes;
    float h = P.health;
    int f = 0;
    if (player) {
        if (h < 0.4f && frames > 1) f = 1;
        else if (h > 1.6f && frames > 2) f = 2;
    } else {
        if (h > 1.6f && frames > 1) f = 1;
        else if (h < 0.4f && frames > 2) f = 2;
    }
    s->static_frame = f;
    float fw = s->base->frames[f].fw;
    float target = 150 * size;
    s->scale_x = s->scale_y = target / (fw > 0 ? fw : 150);
    sprite_update_hitbox(s);
    float px = bar_x + bar_w * (1 - h / 2);
    if (player) s->x = px - 26;
    else s->x = px - (s->width - 26);
    s->y = bar_y - s->height / 2;
    sprite_draw(s, 0, 0);
}

static void draw_hud(void)
{
    gfx_set_camera(&P.hud);
    draw_line(&P.lines[0]);
    draw_line(&P.lines[1]);

    sprite *bg = &P.health_bg;
    float bx = bg->x + 4, by = bg->y + 4, bw = bg->width - 8, bh = bg->height - 8;
    sprite_draw(bg, 0, 0);
    float split = bw * (1 - P.health_lerp / 2);
    gfx_rect(bx, by, split, bh, rgba(255, 0, 0, 255));
    gfx_rect(bx + split, by, bw - split, bh, rgba(102, 255, 51, 255));
    draw_icon(&P.icon_p1, P.icon_size1 * P.bf.icon_scale, true, bx, bw, by);
    draw_icon(&P.icon_p2, P.icon_size2 * P.dad.icon_scale, false, bx, bw, by);

    char buf[96];
    if (opt.botplay) snprintf(buf, sizeof(buf), "Bot Play Enabled");
    else snprintf(buf, sizeof(buf), "Score: %d  Misses: %d", P.score, P.misses);
    text_draw_outline(buf, bg->x + bg->width - 190 + 190, bg->y + 30, 20, 0xffffffff, 0x000000ff, 1.5f, ALIGN_RIGHT);

    for (int i = 0; i < 24; i++)
        if (P.pops[i].used) sprite_draw(&P.pops[i].spr, 0, 0);
    for (int i = 1; i < 4; i++)
        if (P.count_alpha[i] > 0) {
            P.count_spr[i].alpha = ease_apply("cubeInOut", P.count_alpha[i]);
            sprite_draw(&P.count_spr[i], 0, 0);
        }
}

static void draw_pause(void)
{
    gfx_set_camera(NULL);
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, (int)(P.pause_alpha * 255)));
    char buf[96];
    snprintf(buf, sizeof(buf), "%s", P.s.name);
    text_draw(buf, GAME_W - 20, 15, 32, 0xffffffff, ALIGN_RIGHT);
    snprintf(buf, sizeof(buf), "Difficulty: %s", playreq.difficulty);
    text_draw(buf, GAME_W - 20, 55, 32, 0xffffffff, ALIGN_RIGHT);
    for (int i = 0; i < NPAUSE; i++) {
        int rel = i - P.pause_sel;
        float y = GAME_H * 0.48f + rel * 120 * 1.3f - 30;
        float x = 90 + rel * 20;
        char item[48];
        if (i == 2) snprintf(item, sizeof(item), "Botplay %s", opt.botplay ? "on" : "off");
        else snprintf(item, sizeof(item), "%s", pause_items[i]);
        atext_draw(AFONT_BOLD, item, x, y, 1.0f, rel == 0 ? 0xffffffff : rgba(255, 255, 255, 153), P.time);
    }
}

static void draw_results(void)
{
    gfx_set_camera(NULL);
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, (int)(fminf(1, P.results_t * 2) * 200)));
    atext_draw(AFONT_BOLD, "RESULTS", 80, 60, 1.0f, 0xffffffff, P.time);
    char buf[96];
    static const char *const names[5] = {"Sick", "Good", "Bad", "Shit", "Missed"};
    static const u32 cols[5] = {0x89e59eff, 0x89c9e5ff, 0xe6cf8aff, 0xe68c8aff, 0xc68ae6ff};
    float y = 200;
    snprintf(buf, sizeof(buf), "Total notes: %d", P.total_notes);
    text_draw(buf, 100, y, 32, 0xffffffff, ALIGN_LEFT);
    y += 50;
    snprintf(buf, sizeof(buf), "Max combo: %d", P.max_combo);
    text_draw(buf, 100, y, 32, 0xffffffff, ALIGN_LEFT);
    y += 60;
    for (int i = 0; i < 5; i++) {
        snprintf(buf, sizeof(buf), "%s: %d", names[i], P.tally[i]);
        text_draw(buf, 100, y, 32, cols[i], ALIGN_LEFT);
        y += 42;
    }
    snprintf(buf, sizeof(buf), "Score: %d", playreq.story ? playreq.week_score : P.score);
    text_draw(buf, GAME_W - 100, 560, 48, 0xffffffff, ALIGN_RIGHT);
    float acc = P.total_notes ? 100.0f * (P.tally[0] + P.tally[1]) / P.total_notes : 0;
    snprintf(buf, sizeof(buf), "%.1f%%", acc);
    text_draw(buf, GAME_W - 100, 620, 32, 0xffff66ff, ALIGN_RIGHT);
    text_draw("Press A to continue", GAME_W / 2, 670, 24, 0xbbbbbbff, ALIGN_CENTER);
}

void play_draw(void)
{
    if (!P.loaded) return;
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, 255));
    P.cam.zoom = P.cam.zoom > 0 ? P.cam.zoom : 1;
    gfx_set_camera(&P.cam);
    if (P.dead) {
        sprite_draw(&P.bf.spr, P.cam_x, P.cam_y);
        return;
    }
    character *chars[3] = {&P.gf, &P.dad, &P.bf};
    stage_draw(&P.st, chars, 3, P.cam_x, P.cam_y);
    draw_hud();
    if (P.paused) draw_pause();
    if (P.results) draw_results();
}
