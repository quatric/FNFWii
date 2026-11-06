/* Title screen, main menu, story mode, freeplay and options. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "song.h"

/* ------------------------------------------------------------ shared */

static cJSON *ui_json;

static cJSON *ui_entry(const char *key)
{
    if (!ui_json) ui_json = fs_json("ui.json");
    return cJSON_GetObjectItem(ui_json, key);
}

const char *ui_anim_key(const char *key, int index)
{
    cJSON *k = cJSON_GetArrayItem(cJSON_GetObjectItem(ui_entry(key), "keys"), index);
    return cJSON_IsString(k) ? k->valuestring : "";
}

void ui_sprite(sprite *s, const char *key)
{
    sprite_init(s);
    const char *atl = jstr(ui_entry(key), "atlas", NULL);
    if (atl) sprite_load(s, atl);
}

static const char *ui_atlas(const char *key) { return jstr(ui_entry(key), "atlas", ""); }

/* The music's beat for menus that bop along (freakyMenu: 102 BPM). */
static double menu_bpm = 102;
static int menu_beat(void) { return (int)floor(music_time_ms() / (60000.0 / menu_bpm)); }

static void center_item(sprite *s)
{
    /* AtlasMenuItem with centered = true: the position is the middle */
    sprite_update_hitbox(s);
    s->offset_x = s->origin_x;
    s->offset_y = s->origin_y;
}

/* ------------------------------------------------------------ title */

static struct {
    sprite logo, gf, enter;
    char wacky[2][64];
    int last_beat;
    bool skipped, pressed;
    float flash, press_t;
    char lines[4][64];
    int nlines;
    float t;
} T;

static bool title_seen;

static void pick_wacky(void)
{
    strcpy(T.wacky[0], "shoutouts to tom fulp");
    strcpy(T.wacky[1], "lmao");
    char *txt = fs_read("introText.txt", NULL);
    if (!txt) return;
    int n = 0;
    for (char *p = txt; *p; p++)
        if (*p == '\n') n++;
    int want = n > 0 ? rand() % n : 0, i = 0;
    for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n"), i++) {
        if (i != want) continue;
        char *sep = strstr(line, "--");
        if (sep) {
            *sep = 0;
            snprintf(T.wacky[0], 64, "%s", line);
            snprintf(T.wacky[1], 64, "%s", sep + 2);
        }
        break;
    }
    free(txt);
}

void title_enter(void)
{
    memset(&T, 0, sizeof(T));
    menu_music_start();
    ui_sprite(&T.logo, "logo");
    sprite_add_anim(&T.logo, "bump", ui_atlas("logo"), ui_anim_key("logo", 0), 24, false, 0, 0, false);
    sprite_play(&T.logo, "bump", true);
    sprite_update_hitbox(&T.logo);
    T.logo.x = -150;
    T.logo.y = -100;
    ui_sprite(&T.gf, "gfTitle");
    sprite_add_anim(&T.gf, "danceLeft", ui_atlas("gfTitle"), ui_anim_key("gfTitle", 0), 24, false, 0, 0, false);
    sprite_add_anim(&T.gf, "danceRight", ui_atlas("gfTitle"), ui_anim_key("gfTitle", 1), 24, false, 0, 0, false);
    sprite_play(&T.gf, "danceRight", true);
    sprite_update_hitbox(&T.gf);
    T.gf.x = GAME_W * 0.4f;
    T.gf.y = GAME_H * 0.07f;
    ui_sprite(&T.enter, "titleEnter");
    sprite_add_anim(&T.enter, "idle", ui_atlas("titleEnter"), ui_anim_key("titleEnter", 0), 24, true, 0, 0, false);
    sprite_add_anim(&T.enter, "press", ui_atlas("titleEnter"), ui_anim_key("titleEnter", 1), 24, true, 0, 0, false);
    sprite_play(&T.enter, "idle", true);
    sprite_update_hitbox(&T.enter);
    T.enter.x = 100;
    T.enter.y = GAME_H * 0.8f;
    pick_wacky();
    T.skipped = title_seen;
    T.last_beat = -1;
}

void title_exit(void)
{
    sprite_free(&T.logo);
    sprite_free(&T.gf);
    sprite_free(&T.enter);
}

static void cool_text(const char *a, const char *b)
{
    T.nlines = 0;
    if (a) snprintf(T.lines[T.nlines++], 64, "%s", a);
    if (b) snprintf(T.lines[T.nlines++], 64, "%s", b);
}

static void more_text(const char *a)
{
    if (T.nlines < 4) snprintf(T.lines[T.nlines++], 64, "%s", a);
}

static void skip_intro(void)
{
    if (T.skipped) return;
    T.skipped = true;
    title_seen = true;
    T.flash = opt.flashing ? 1.0f : 0;
}

void title_update(float dt)
{
    T.t += dt;
    int beat = menu_beat();
    while (T.last_beat < beat) {
        T.last_beat++;
        int b = T.last_beat;
        sprite_play(&T.logo, "bump", true);
        sprite_play(&T.gf, (b & 1) ? "danceLeft" : "danceRight", false);
        if (!T.skipped) {
            switch (b) {
            case 1: cool_text("The", "Funkin Crew Inc"); break;
            case 3: more_text("presents"); break;
            case 4: T.nlines = 0; break;
            case 5: cool_text("In association", "with"); break;
            case 7: more_text("newgrounds"); break;
            case 8: T.nlines = 0; break;
            case 9: cool_text(T.wacky[0], NULL); break;
            case 11: more_text(T.wacky[1]); break;
            case 12: T.nlines = 0; break;
            case 13: more_text("Friday"); break;
            case 14: more_text("Night"); break;
            case 15: more_text("Funkin"); break;
            case 16: skip_intro(); break;
            }
        }
    }
    sprite_update(&T.logo, dt);
    sprite_update(&T.gf, dt);
    sprite_update(&T.enter, dt);
    if (T.flash > 0) T.flash -= dt / 1.0f;
    u32 pr = in_pressed();
    if (!T.skipped) {
        if (pr & (ACT_ACCEPT | ACT_PAUSE)) skip_intro();
        return;
    }
    if (T.pressed) {
        T.press_t += dt;
        if (T.press_t > 2.0f) game_switch(SCENE_MAINMENU);
        return;
    }
    if (pr & (ACT_ACCEPT | ACT_PAUSE)) {
        T.pressed = true;
        sprite_play(&T.enter, "press", true);
        if (opt.flashing) T.flash = 1;
        sfx_confirm();
    }
    if (pr & (ACT_BACK | ACT_HOME)) game_switch(SCENE_QUIT);
}

void title_draw(void)
{
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, 255));
    if (!T.skipped) {
        for (int i = 0; i < T.nlines; i++) {
            float w = atext_width(AFONT_BOLD, T.lines[i], 1);
            atext_draw(AFONT_BOLD, T.lines[i], (GAME_W - w) / 2, 200 + i * 60, 1, 0xffffffff, T.t);
        }
        return;
    }
    sprite_draw(&T.logo, 0, 0);
    sprite_draw(&T.gf, 0, 0);
    sprite_draw(&T.enter, 0, 0);
    if (T.flash > 0) gfx_rect(0, 0, GAME_W, GAME_H, rgba(255, 255, 255, (int)(fminf(1, T.flash) * 255)));
}

/* ------------------------------------------------------------ main menu */

static const char *const mm_names[3] = {"storymode", "freeplay", "options"};
#define MM_N 3
static struct {
    sprite bg, magenta;
    sprite items[MM_N];
    int sel;
    bool chosen;
    float chosen_t;
    float cam_y, follow_y;
} M;
static int mm_remember;

static void mm_select(int i)
{
    for (int k = 0; k < MM_N; k++) {
        sprite_play(&M.items[k], k == i ? "selected" : "idle", true);
        center_item(&M.items[k]);
    }
    M.sel = i;
    M.follow_y = M.items[i].y;
}

void mainmenu_enter(void)
{
    memset(&M, 0, sizeof(M));
    menu_music_start();
    ui_sprite(&M.bg, "menuBG");
    sprite_update_hitbox(&M.bg);
    M.bg.scale_x = M.bg.scale_y = 1.2f;
    sprite_update_hitbox(&M.bg);
    sprite_screen_center(&M.bg, true, true);
    M.bg.sf_x = 0;
    M.bg.sf_y = 0.17f;
    ui_sprite(&M.magenta, "menuBGMagenta");
    M.magenta.scale_x = M.magenta.scale_y = 1.2f;
    sprite_update_hitbox(&M.magenta);
    M.magenta.x = M.bg.x;
    M.magenta.y = M.bg.y;
    M.magenta.sf_x = 0;
    M.magenta.sf_y = 0.17f;
    M.magenta.visible = false;
    float spacing = 160, top = (GAME_H - spacing * (MM_N - 1)) / 2;
    for (int i = 0; i < MM_N; i++) {
        char key[32];
        snprintf(key, sizeof(key), "menu_%s", mm_names[i]);
        ui_sprite(&M.items[i], key);
        char k[64];
        snprintf(k, sizeof(k), "sparrow|%s idle|", mm_names[i]);
        sprite_add_anim(&M.items[i], "idle", ui_atlas(key), k, 24, true, 0, 0, false);
        snprintf(k, sizeof(k), "sparrow|%s selected|", mm_names[i]);
        sprite_add_anim(&M.items[i], "selected", ui_atlas(key), k, 24, true, 0, 0, false);
        M.items[i].x = GAME_W / 2;
        M.items[i].y = top + spacing * i;
        M.items[i].sf_x = 0;
        M.items[i].sf_y = 0.4f;
    }
    mm_select(mm_remember);
    M.cam_y = M.follow_y - GAME_H / 2;
}

void mainmenu_exit(void)
{
    sprite_free(&M.bg);
    sprite_free(&M.magenta);
    for (int i = 0; i < MM_N; i++) sprite_free(&M.items[i]);
}

void mainmenu_update(float dt)
{
    for (int i = 0; i < MM_N; i++) sprite_update(&M.items[i], dt);
    M.cam_y += (M.follow_y - GAME_H / 2 - M.cam_y) * (1 - powf(1 - 0.06f, dt * 60));
    if (M.chosen) {
        M.chosen_t += dt;
        if (opt.flashing) M.magenta.visible = fmodf(M.chosen_t, 0.15f) < 0.075f;
        M.items[M.sel].visible = fmodf(M.chosen_t, 0.12f) < 0.06f;
        if (M.chosen_t > 1.0f) {
            mm_remember = M.sel;
            if (M.sel == 0) game_switch(SCENE_STORY);
            else if (M.sel == 1) game_switch(SCENE_FREEPLAY);
            else game_switch(SCENE_OPTIONS);
        }
        return;
    }
    u32 nav = in_nav(), pr = in_pressed();
    if (nav & ACT_UP) {
        mm_select((M.sel + MM_N - 1) % MM_N);
        sfx_scroll();
    }
    if (nav & ACT_DOWN) {
        mm_select((M.sel + 1) % MM_N);
        sfx_scroll();
    }
    if (pr & ACT_ACCEPT) {
        M.chosen = true;
        sfx_confirm();
    }
    if (pr & ACT_BACK) {
        sfx_cancel();
        game_switch(SCENE_TITLE);
    }
    if (pr & ACT_HOME) game_switch(SCENE_QUIT);
}

void mainmenu_draw(void)
{
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, 255));
    sprite_draw(&M.bg, 0, M.cam_y);
    sprite_draw(&M.magenta, 0, M.cam_y);
    for (int i = 0; i < MM_N; i++) sprite_draw(&M.items[i], 0, M.cam_y);
    text_draw_outline("FNFWii v" APP_VERSION, 5, GAME_H - 26, 20, 0xffffffff, 0x000000ff, 1.5f, ALIGN_LEFT);
    text_draw_outline(in_layout_hint(), GAME_W - 5, GAME_H - 26, 20, 0xffffffff, 0x000000ff, 1.5f, ALIGN_RIGHT);
}

/* ------------------------------------------------------------ week list (story + freeplay) */

typedef struct {
    char id[32];
    char name[64];
    char title[128];
    u32 bg;
    char songs[8][48];
    int nsongs;
    char diffs[6][16];
    int ndiffs;
    cJSON *props;
} week_info;

static week_info weeks[16];
static int nweeks;
static cJSON *weeks_json;

static void weeks_load(void)
{
    if (weeks_json) return;
    weeks_json = fs_json("weeks.json");
    cJSON *w;
    nweeks = 0;
    cJSON_ArrayForEach(w, weeks_json)
    {
        if (nweeks >= 16) break;
        week_info *wi = &weeks[nweeks++];
        memset(wi, 0, sizeof(*wi));
        strncpy(wi->id, jstr(w, "id", ""), sizeof(wi->id) - 1);
        strncpy(wi->name, jstr(w, "name", ""), sizeof(wi->name) - 1);
        strncpy(wi->title, jstr(w, "title", ""), sizeof(wi->title) - 1);
        wi->bg = parse_color(jstr(w, "background", "#F9CF51"), 0xF9CF51ff);
        cJSON *s;
        cJSON_ArrayForEach(s, cJSON_GetObjectItem(w, "songs"))
        {
            if (wi->nsongs < 8 && cJSON_IsString(s)) strncpy(wi->songs[wi->nsongs++], s->valuestring, 47);
        }
        cJSON_ArrayForEach(s, cJSON_GetObjectItem(w, "difficulties"))
        {
            if (wi->ndiffs < 6 && cJSON_IsString(s)) strncpy(wi->diffs[wi->ndiffs++], s->valuestring, 15);
        }
        wi->props = cJSON_GetObjectItem(w, "props");
    }
}

/* ------------------------------------------------------------ story mode */

static struct {
    int sel, diff;
    sprite titles[16];
    sprite diff_spr;
    sprite arrows[2];
    sprite props[3];
    int nprops;
    int prop_week;
    float sel_y;
    bool chosen;
    float chosen_t;
    int last_beat;
    float t;
    float score_lerp;
    char song_names[8][64];
} S;
static int story_remember, story_diff_remember = 1;

static const char *story_diff(void)
{
    week_info *w = &weeks[S.sel];
    if (w->ndiffs == 0) return "normal";
    return w->diffs[S.diff % w->ndiffs];
}

static void story_load_diff(void)
{
    sprite_free(&S.diff_spr);
    char key[32];
    snprintf(key, sizeof(key), "diff_%s", story_diff());
    ui_sprite(&S.diff_spr, key);
    if (!strcmp(story_diff(), "nightmare")) {
        char k[64];
        snprintf(k, sizeof(k), "sparrow|idle|");
        sprite_add_anim(&S.diff_spr, "idle", ui_atlas(key), "sparrow|idle|", 24, true, 0, 0, false);
        sprite_play(&S.diff_spr, "idle", true);
    }
    sprite_update_hitbox(&S.diff_spr);
    S.diff_spr.x = 1280 - 410 + 20 + (310 - S.diff_spr.width) / 2;
    S.diff_spr.y = 480 + (60 - S.diff_spr.height) / 2;
}

static void story_load_props(void)
{
    for (int i = 0; i < S.nprops; i++) sprite_free(&S.props[i]);
    S.nprops = 0;
    cJSON *p;
    int idx = 0;
    cJSON_ArrayForEach(p, weeks[S.sel].props)
    {
        if (S.nprops >= 3) break;
        sprite *s = &S.props[S.nprops];
        sprite_init(s);
        const char *atl = jstr(p, "atlas", "");
        sprite_load(s, atl);
        s->use_anim_offsets = true;
        sprite_add_anims_json(s, cJSON_GetObjectItem(p, "anims"), atl);
        float sc = jnum(p, "scale", 1);
        s->scale_x = s->scale_y = sc;
        if (sprite_has_anim(s, "danceLeft")) sprite_play(s, "danceLeft", true);
        else sprite_play(s, "idle", true);
        sprite_update_hitbox(s);
        cJSON *off = cJSON_GetObjectItem(p, "offsets");
        /* LevelProp: three slots across the yellow band */
        s->x = (GAME_W * 0.25f) * (1 + idx) - 150 + jidx(off, 0, 0);
        s->y = 56 + jidx(off, 1, 0);
        S.nprops++;
        idx++;
    }
    S.prop_week = S.sel;
    for (int i = 0; i < weeks[S.sel].nsongs; i++) {
        song tmp;
        if (song_load_meta(&tmp, weeks[S.sel].songs[i], "default")) {
            snprintf(S.song_names[i], 64, "%s", tmp.name);
            song_free(&tmp);
        } else {
            snprintf(S.song_names[i], 64, "%s", weeks[S.sel].songs[i]);
        }
    }
}

void story_enter(void)
{
    memset(&S, 0, sizeof(S));
    weeks_load();
    menu_music_start();
    S.sel = story_remember < nweeks ? story_remember : 0;
    S.diff = story_diff_remember;
    for (int i = 0; i < nweeks; i++) {
        sprite_init(&S.titles[i]);
        sprite_load(&S.titles[i], weeks[i].title);
        sprite_update_hitbox(&S.titles[i]);
    }
    for (int i = 0; i < 2; i++) {
        ui_sprite(&S.arrows[i], "storyArrows");
        sprite_add_anim(&S.arrows[i], "idle", ui_atlas("storyArrows"), i ? "sparrow|rightIdle|" : "sparrow|leftIdle|", 24,
                        false, 0, 0, false);
        sprite_add_anim(&S.arrows[i], "press", ui_atlas("storyArrows"),
                        i ? "sparrow|rightConfirm|" : "sparrow|leftConfirm|", 24, false, 0, 0, false);
        sprite_play(&S.arrows[i], "idle", true);
        sprite_update_hitbox(&S.arrows[i]);
        S.arrows[i].x = i ? 1280 - 80 : 1280 - 410;
        S.arrows[i].y = 480;
    }
    story_load_diff();
    story_load_props();
    S.sel_y = S.sel;
    S.last_beat = -1;
}

void story_exit(void)
{
    for (int i = 0; i < nweeks; i++) sprite_free(&S.titles[i]);
    for (int i = 0; i < S.nprops; i++) sprite_free(&S.props[i]);
    sprite_free(&S.diff_spr);
    sprite_free(&S.arrows[0]);
    sprite_free(&S.arrows[1]);
}

void story_update(float dt)
{
    S.t += dt;
    S.sel_y = lerpf(S.sel_y, S.sel, 1 - powf(0.8f, dt * 60));
    int beat = menu_beat();
    while (S.last_beat < beat) {
        S.last_beat++;
        for (int i = 0; i < S.nprops; i++) {
            sprite *p = &S.props[i];
            if (sprite_has_anim(p, "danceLeft"))
                sprite_play(p, (S.last_beat & 1) ? "danceRight" : "danceLeft", true);
            else if (!S.chosen || i != 1) sprite_play(p, "idle", true);
        }
    }
    for (int i = 0; i < S.nprops; i++) sprite_update(&S.props[i], dt);
    for (int i = 0; i < 2; i++) {
        sprite_update(&S.arrows[i], dt);
        if (sprite_anim_finished(&S.arrows[i]) && !strcmp(sprite_anim_name(&S.arrows[i]), "press") &&
            !(in_held() & (i ? ACT_RIGHT : ACT_LEFT)))
            sprite_play(&S.arrows[i], "idle", true);
    }
    sprite_update(&S.diff_spr, dt);
    S.score_lerp = lerpf(S.score_lerp, save_week_score(weeks[S.sel].id, story_diff()), 1 - powf(0.5f, dt * 60));
    if (S.chosen) {
        S.chosen_t += dt;
        if (S.chosen_t > 1.0f) {
            week_info *w = &weeks[S.sel];
            memset(&playreq, 0, sizeof(playreq));
            for (int i = 0; i < w->nsongs; i++) strcpy(playreq.songs[i], w->songs[i]);
            playreq.nsongs = w->nsongs;
            strcpy(playreq.difficulty, story_diff());
            strcpy(playreq.variation, !strcmp(story_diff(), "erect") || !strcmp(story_diff(), "nightmare") ? "erect" : "default");
            strncpy(playreq.week, w->id, sizeof(playreq.week) - 1);
            playreq.story = true;
            game_switch(SCENE_PLAY);
        }
        return;
    }
    u32 nav = in_nav(), pr = in_pressed();
    if (nav & (ACT_UP | ACT_DOWN)) {
        S.sel = (S.sel + (nav & ACT_UP ? nweeks - 1 : 1)) % nweeks;
        story_remember = S.sel;
        sfx_scroll();
        story_load_props();
        story_load_diff();
    }
    if (nav & (ACT_LEFT | ACT_RIGHT)) {
        int n = weeks[S.sel].ndiffs ? weeks[S.sel].ndiffs : 1;
        S.diff = (S.diff + (nav & ACT_LEFT ? n - 1 : 1)) % n;
        story_diff_remember = S.diff;
        sprite_play(&S.arrows[(nav & ACT_RIGHT) ? 1 : 0], "press", true);
        story_load_diff();
        sfx_scroll();
    }
    if (pr & ACT_ACCEPT) {
        S.chosen = true;
        sfx_confirm();
        if (S.nprops > 1 && sprite_has_anim(&S.props[1], "confirm")) sprite_play(&S.props[1], "confirm", true);
    }
    if (pr & ACT_BACK) {
        sfx_cancel();
        game_switch(SCENE_MAINMENU);
    }
}

void story_draw(void)
{
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, 255));
    /* week titles scroll under the band */
    for (int i = 0; i < nweeks; i++) {
        sprite *t = &S.titles[i];
        t->x = (GAME_W - t->width) / 2;
        t->y = 480 + (i - S.sel_y) * 120;
        if (t->y < 380 || t->y > GAME_H + 20) continue;
        u32 col = 0xffffffff;
        if (i == S.sel && S.chosen && opt.flashing && fmodf(S.chosen_t, 0.1f) < 0.05f) col = rgba(51, 255, 255, 255);
        t->color = col;
        t->alpha = i == S.sel ? 1.0f : 0.6f;
        sprite_draw(t, 0, 0);
    }
    gfx_rect(0, 0, GAME_W, 56, rgba(0, 0, 0, 255));
    gfx_rect(0, 56, GAME_W, 400, weeks[S.sel].bg);
    for (int i = 0; i < S.nprops; i++) sprite_draw(&S.props[i], 0, 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "LEVEL SCORE: %d", (int)roundf(S.score_lerp));
    text_draw(buf, 10, 10, 32, 0xffffffff, ALIGN_LEFT);
    text_draw(weeks[S.sel].name, GAME_W - 10, 10, 32, rgba(255, 255, 255, 178), ALIGN_RIGHT);
    text_draw("TRACKS", 120, 500, 32, rgba(229, 87, 119, 255), ALIGN_CENTER);
    for (int i = 0; i < weeks[S.sel].nsongs; i++)
        text_draw(S.song_names[i], 120, 550 + i * 32, 28, rgba(229, 87, 119, 255), ALIGN_CENTER);
    sprite_draw(&S.arrows[0], 0, 0);
    sprite_draw(&S.diff_spr, 0, 0);
    sprite_draw(&S.arrows[1], 0, 0);
}

/* ------------------------------------------------------------ freeplay */

typedef struct {
    char id[48];
    char name[64];
    char icon[128];
    char vars[4][16];
    int nvars;
} fp_song;

static struct {
    fp_song songs[96];
    int n;
    int sel;
    float sel_y;
    int diff;
    int var;
    sprite bg;
    sprite icons[96];
    float score_lerp;
    float t;
    char diffs[8][16];
    int ndiffs;
    bool chosen;
    float chosen_t;
    u32 bg_color, bg_target;
} F;
static int fp_remember, fp_diff_remember = 1;

static void fp_refresh_diffs(void)
{
    fp_song *s = &F.songs[F.sel];
    song meta;
    F.ndiffs = 0;
    const char *var = F.var < s->nvars ? s->vars[F.var] : "default";
    if (song_load_meta(&meta, s->id, var)) {
        for (int i = 0; i < meta.ndifficulties; i++) strcpy(F.diffs[F.ndiffs++], meta.difficulties[i]);
        song_free(&meta);
    }
    if (F.ndiffs == 0) {
        strcpy(F.diffs[0], "normal");
        F.ndiffs = 1;
    }
    if (F.diff >= F.ndiffs) F.diff = F.ndiffs - 1;
}

void freeplay_enter(void)
{
    memset(&F, 0, sizeof(F));
    weeks_load();
    menu_music_start();
    for (int w = 0; w < nweeks; w++) {
        for (int k = 0; k < weeks[w].nsongs && F.n < 96; k++) {
            fp_song *s = &F.songs[F.n];
            song meta;
            if (!song_load_meta(&meta, weeks[w].songs[k], "default")) continue;
            strncpy(s->id, weeks[w].songs[k], sizeof(s->id) - 1);
            strncpy(s->name, meta.name, sizeof(s->name) - 1);
            char rel[96];
            snprintf(rel, sizeof(rel), "chars/%s.json", meta.opponent);
            cJSON *c = fs_json(rel);
            strncpy(s->icon, jstr(c, "icon", ""), sizeof(s->icon) - 1);
            cJSON_Delete(c);
            strcpy(s->vars[s->nvars++], "default");
            /* other mixes from the song list */
            cJSON *all = fs_json("songs.json");
            cJSON *e;
            cJSON_ArrayForEach(e, all)
            {
                if (strcmp(jstr(e, "id", ""), s->id)) continue;
                cJSON *v;
                cJSON_ArrayForEach(v, cJSON_GetObjectItem(e, "variations"))
                {
                    if (s->nvars < 4 && cJSON_IsString(v)) strncpy(s->vars[s->nvars++], v->valuestring, 15);
                }
            }
            cJSON_Delete(all);
            song_free(&meta);
            sprite_init(&F.icons[F.n]);
            sprite_load(&F.icons[F.n], s->icon);
            float fw = F.icons[F.n].frame_w > 0 ? F.icons[F.n].frame_w : 150;
            F.icons[F.n].scale_x = F.icons[F.n].scale_y = 150 / fw;
            sprite_update_hitbox(&F.icons[F.n]);
            F.n++;
        }
    }
    ui_sprite(&F.bg, "menuDesat");
    sprite_update_hitbox(&F.bg);
    sprite_screen_center(&F.bg, true, true);
    F.sel = fp_remember < F.n ? fp_remember : 0;
    F.diff = fp_diff_remember;
    F.sel_y = F.sel;
    F.bg_color = F.bg_target = weeks[0].bg;
    if (F.n) fp_refresh_diffs();
}

void freeplay_exit(void)
{
    for (int i = 0; i < F.n; i++) sprite_free(&F.icons[i]);
    sprite_free(&F.bg);
}

static u32 lerp_color(u32 a, u32 b, float t)
{
    int r = (int)lerpf(a >> 24, b >> 24, t), g = (int)lerpf((a >> 16) & 0xff, (b >> 16) & 0xff, t);
    int bl = (int)lerpf((a >> 8) & 0xff, (b >> 8) & 0xff, t);
    return rgba(r, g, bl, 255);
}

static u32 fp_week_color(int idx)
{
    int n = 0;
    for (int w = 0; w < nweeks; w++) {
        n += weeks[w].nsongs;
        if (idx < n) return weeks[w].bg;
    }
    return 0x9271fdff;
}

void freeplay_update(float dt)
{
    F.t += dt;
    F.sel_y = lerpf(F.sel_y, F.sel, 1 - powf(0.8f, dt * 60));
    F.bg_color = lerp_color(F.bg_color, F.bg_target, 1 - powf(0.9f, dt * 60));
    if (F.n == 0) {
        if (in_pressed() & ACT_BACK) game_switch(SCENE_MAINMENU);
        return;
    }
    fp_song *s = &F.songs[F.sel];
    const char *var = F.var < s->nvars ? s->vars[F.var] : "default";
    F.score_lerp = lerpf(F.score_lerp, save_score(s->id, F.diffs[F.diff], var), 1 - powf(0.6f, dt * 60));
    if (F.chosen) {
        F.chosen_t += dt;
        if (F.chosen_t > 0.6f) {
            memset(&playreq, 0, sizeof(playreq));
            strcpy(playreq.songs[0], s->id);
            playreq.nsongs = 1;
            strcpy(playreq.difficulty, F.diffs[F.diff]);
            strcpy(playreq.variation, var);
            game_switch(SCENE_PLAY);
        }
        return;
    }
    u32 nav = in_nav(), pr = in_pressed();
    if (nav & (ACT_UP | ACT_DOWN)) {
        F.sel = (F.sel + (nav & ACT_UP ? F.n - 1 : 1)) % F.n;
        fp_remember = F.sel;
        F.var = 0;
        F.bg_target = fp_week_color(F.sel);
        fp_refresh_diffs();
        sfx_scroll();
    }
    if (nav & (ACT_LEFT | ACT_RIGHT)) {
        F.diff = (F.diff + (nav & ACT_LEFT ? F.ndiffs - 1 : 1)) % F.ndiffs;
        fp_diff_remember = F.diff;
        sfx_scroll();
    }
    if ((in_held() & ACT_RESET) && (pr & ACT_RESET)) {
        /* minus: next mix (erect, pico, ...) */
        if (s->nvars > 1) {
            F.var = (F.var + 1) % s->nvars;
            fp_refresh_diffs();
            sfx_scroll();
        }
    }
    if (pr & ACT_ACCEPT) {
        F.chosen = true;
        sfx_confirm();
    }
    if (pr & ACT_BACK) {
        sfx_cancel();
        game_switch(SCENE_MAINMENU);
    }
}

void freeplay_draw(void)
{
    F.bg.color = F.bg_color;
    sprite_draw(&F.bg, 0, 0);
    for (int i = 0; i < F.n; i++) {
        float rel = i - F.sel_y;
        float y = rel * 120 * 1.3f + GAME_H * 0.48f - 30;
        if (y < -150 || y > GAME_H + 50) continue;
        float x = 90 + rel * 20;
        u32 col = i == F.sel ? 0xffffffff : rgba(255, 255, 255, 153);
        if (i == F.sel && F.chosen && fmodf(F.chosen_t, 0.1f) < 0.05f) col = rgba(51, 255, 255, 255);
        atext_draw(AFONT_BOLD, F.songs[i].name, x, y, 1.0f, col, F.t);
        sprite *ic = &F.icons[i];
        ic->x = x + atext_width(AFONT_BOLD, F.songs[i].name, 1) + 10;
        ic->y = y - 30;
        ic->alpha = i == F.sel ? 1.0f : 0.6f;
        sprite_draw(ic, 0, 0);
    }
    if (F.n == 0) {
        text_draw("No songs found.", GAME_W / 2, GAME_H / 2, 32, 0xffffffff, ALIGN_CENTER);
        return;
    }
    fp_song *s = &F.songs[F.sel];
    float bw = 420;
    gfx_rect(GAME_W - bw, 0, bw, 106, rgba(0, 0, 0, 153));
    char buf[96];
    snprintf(buf, sizeof(buf), "PERSONAL BEST: %d", (int)roundf(F.score_lerp));
    text_draw(buf, GAME_W - bw + 10, 8, 30, 0xffffffff, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "< %s >", F.diffs[F.diff]);
    for (char *p = buf; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    text_draw(buf, GAME_W - bw / 2, 44, 26, 0xffffffff, ALIGN_CENTER);
    if (s->nvars > 1) {
        snprintf(buf, sizeof(buf), "Mix: %s (- to change)", F.var < s->nvars ? s->vars[F.var] : "default");
        text_draw(buf, GAME_W - bw / 2, 76, 20, 0xffff66ff, ALIGN_CENTER);
    }
}

/* ------------------------------------------------------------ options */

enum { O_DOWNSCROLL, O_MIDDLESCROLL, O_GHOST, O_ZOOM, O_FLASH, O_OFFSET, O_MUSIC, O_SFX, O_WIDE, O_UPDATES, O_N };
static const char *const o_names[O_N] = {"Downscroll", "Middlescroll", "Ghost Tapping", "Camera Zoom", "Flashing Lights",
                                         "Note Offset", "Music Volume", "Sound Volume", "Widescreen", "Check Updates"};
static struct {
    int sel;
    float sel_y;
    sprite bg;
    float t;
} O;

void options_enter(void)
{
    memset(&O, 0, sizeof(O));
    menu_music_start();
    ui_sprite(&O.bg, "menuDesat");
    sprite_update_hitbox(&O.bg);
    sprite_screen_center(&O.bg, true, true);
    O.bg.color = rgba(234, 113, 253, 255);
}

void options_exit(void)
{
    sprite_free(&O.bg);
    save_write();
}

static void o_value(int i, char *buf, size_t n)
{
    switch (i) {
    case O_DOWNSCROLL: snprintf(buf, n, "%s", opt.downscroll ? "on" : "off"); break;
    case O_MIDDLESCROLL: snprintf(buf, n, "%s", opt.middlescroll ? "on" : "off"); break;
    case O_GHOST: snprintf(buf, n, "%s", opt.ghost_tapping ? "on" : "off"); break;
    case O_ZOOM: snprintf(buf, n, "%s", opt.camera_zoom ? "on" : "off"); break;
    case O_FLASH: snprintf(buf, n, "%s", opt.flashing ? "on" : "off"); break;
    case O_OFFSET: snprintf(buf, n, "%d ms", opt.offset_ms); break;
    case O_MUSIC: snprintf(buf, n, "%d", opt.music_volume); break;
    case O_SFX: snprintf(buf, n, "%d", opt.sfx_volume); break;
    case O_WIDE: snprintf(buf, n, "%s", opt.widescreen == 0 ? "auto" : opt.widescreen == 1 ? "16:9" : "4:3"); break;
    case O_UPDATES: snprintf(buf, n, "%s", opt.check_updates ? "on" : "off"); break;
    }
}

static void o_change(int i, int d)
{
    switch (i) {
    case O_DOWNSCROLL: opt.downscroll = !opt.downscroll; break;
    case O_MIDDLESCROLL: opt.middlescroll = !opt.middlescroll; break;
    case O_GHOST: opt.ghost_tapping = !opt.ghost_tapping; break;
    case O_ZOOM: opt.camera_zoom = !opt.camera_zoom; break;
    case O_FLASH: opt.flashing = !opt.flashing; break;
    case O_OFFSET: opt.offset_ms = (int)clampf(opt.offset_ms + d * 5, -500, 500); break;
    case O_MUSIC: opt.music_volume = (int)clampf(opt.music_volume + d, 0, 10); break;
    case O_SFX: opt.sfx_volume = (int)clampf(opt.sfx_volume + d, 0, 10); break;
    case O_WIDE: opt.widescreen = (opt.widescreen + (d < 0 ? 2 : 1)) % 3; break;
    case O_UPDATES: opt.check_updates = !opt.check_updates; break;
    }
    options_apply();
}

void options_update(float dt)
{
    O.t += dt;
    O.sel_y = lerpf(O.sel_y, O.sel, 1 - powf(0.8f, dt * 60));
    u32 nav = in_nav(), pr = in_pressed();
    if (nav & ACT_UP) {
        O.sel = (O.sel + O_N - 1) % O_N;
        sfx_scroll();
    }
    if (nav & ACT_DOWN) {
        O.sel = (O.sel + 1) % O_N;
        sfx_scroll();
    }
    if (nav & (ACT_LEFT | ACT_RIGHT)) {
        o_change(O.sel, nav & ACT_LEFT ? -1 : 1);
        sfx_scroll();
    }
    if (pr & ACT_ACCEPT) {
        o_change(O.sel, 1);
        sfx_confirm();
    }
    if (pr & ACT_BACK) {
        sfx_cancel();
        game_switch(SCENE_MAINMENU);
    }
}

void options_draw(void)
{
    sprite_draw(&O.bg, 0, 0);
    for (int i = 0; i < O_N; i++) {
        float y = GAME_H / 2 - 40 + (i - O.sel_y) * 90;
        if (y < -80 || y > GAME_H) continue;
        u32 col = i == O.sel ? 0xffffffff : rgba(255, 255, 255, 153);
        atext_draw(AFONT_BOLD, o_names[i], 80, y, 0.8f, col, O.t);
        char v[32];
        o_value(i, v, sizeof(v));
        text_draw_outline(v, GAME_W - 80, y + 10, 40, i == O.sel ? 0xffff66ff : 0xffffffff, 0x000000ff, 2, ALIGN_RIGHT);
    }
    text_draw_outline("Left/Right change   B back", GAME_W / 2, GAME_H - 34, 22, 0xffffffff, 0x000000ff, 1.5f,
                      ALIGN_CENTER);
}

void draw_loading(const char *what)
{
    gfx_begin();
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, 255));
    text_draw(what, GAME_W - 30, GAME_H - 60, 32, 0xffffffff, ALIGN_RIGHT);
    gfx_end();
}

void draw_menu_bg(const char *key, u32 tint, float yscroll)
{
    (void)key;
    (void)tint;
    (void)yscroll;
}
