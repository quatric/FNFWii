/* Game-wide state: scenes, options, save data, menu sounds. */
#ifndef FUNKIN_GAME_H
#define FUNKIN_GAME_H

#include <stdbool.h>

#include "audio.h"
#include "gfx.h"
#include "input.h"
#include "sprite.h"
#include "text.h"
#include "util.h"

typedef enum {
    SCENE_NONE,
    SCENE_TITLE,
    SCENE_MAINMENU,
    SCENE_STORY,
    SCENE_FREEPLAY,
    SCENE_OPTIONS,
    SCENE_PLAY,
    SCENE_QUIT,
} scene_id;

typedef struct {
    bool downscroll;
    bool ghost_tapping;
    bool flashing;
    bool camera_zoom;
    bool middlescroll;
    bool botplay;
    int offset_ms;        /* audio/visual latency, ms (positive: notes later) */
    int music_volume;     /* 0..10 */
    int sfx_volume;       /* 0..10 */
    int widescreen;       /* 0 auto, 1 on, 2 off */
    bool check_updates;
} options;

extern options opt;

/* What PlayState should play next (set by story mode / freeplay). */
typedef struct {
    char songs[8][48];
    int nsongs;
    int index;
    char difficulty[16];
    char variation[16];
    bool story;
    char week[32];
    int week_score;
    int week_misses;
    int practice;
} play_request;

extern play_request playreq;

void game_switch(scene_id s);   /* fades out, then switches */
scene_id game_scene(void);
float game_time(void);          /* seconds since start */

/* Menu sounds */
void sfx_scroll(void);
void sfx_confirm(void);
void sfx_cancel(void);
void menu_music_start(void);    /* freakyMenu, if not already playing */
bool menu_music_playing(void);
void menu_music_stop(void);

/* Save data */
void save_load(void);
void save_write(void);
int  save_score(const char *song, const char *diff, const char *variation);
void save_set_score(const char *song, const char *diff, const char *variation, int score, float accuracy);
int  save_week_score(const char *week, const char *diff);
void save_set_week_score(const char *week, const char *diff, int score);
void options_apply(void);

/* Scenes */
void title_enter(void);
void title_update(float dt);
void title_draw(void);
void title_exit(void);

void mainmenu_enter(void);
void mainmenu_update(float dt);
void mainmenu_draw(void);
void mainmenu_exit(void);

void story_enter(void);
void story_update(float dt);
void story_draw(void);
void story_exit(void);

void freeplay_enter(void);
void freeplay_update(float dt);
void freeplay_draw(void);
void freeplay_exit(void);

void options_enter(void);
void options_update(float dt);
void options_draw(void);
void options_exit(void);

void play_enter(void);
void play_update(float dt);
void play_draw(void);
void play_exit(void);
bool play_debug_seek(double ms); /* autotest: jump in the running song */

/* Shared drawing helpers */
void draw_menu_bg(const char *ui_key, u32 tint, float yscroll);
void ui_sprite(sprite *s, const char *ui_key);  /* loads ui.json[key] with its animations */
const char *ui_anim_key(const char *ui_key, int index);
void draw_loading(const char *what);

#endif
