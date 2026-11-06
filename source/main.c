/* FNFWii: Friday Night Funkin' for the Wii. */
#include <fat.h>
#include <gccore.h>
#include <input_wii.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiiupdate.h>
#include <wiiuse/wpad.h>

#include "atlas.h"
#include "autotest.h"
#include "game.h"

options opt = {
    .downscroll = false,
    .ghost_tapping = false,
    .flashing = true,
    .camera_zoom = true,
    .offset_ms = 0,
    .music_volume = 10,
    .sfx_volume = 10,
    .widescreen = 0,
    .check_updates = true,
};
play_request playreq;

static scene_id current = SCENE_NONE, pending = SCENE_NONE;
static float fade = 1.0f;       /* 1 = black */
static int fade_dir = -1;       /* -1 fading in, +1 fading out */
static float elapsed_total;
static sound *snd_scroll, *snd_confirm, *snd_cancel;
static bool power_off, reset_pressed;

static void on_power(void) { power_off = true; }
static void on_reset(u32 irq, void *ctx)
{
    (void)irq;
    (void)ctx;
    reset_pressed = true;
}
static void on_wpad_power(s32 chan) { (void)chan; power_off = true; }

scene_id game_scene(void) { return current; }
float game_time(void) { return elapsed_total; }

void game_switch(scene_id s)
{
    if (pending != SCENE_NONE) return;
    pending = s;
    fade_dir = 1;
}

void sfx_scroll(void) { sound_play(snd_scroll, 0.4f); }
void sfx_confirm(void) { sound_play(snd_confirm, 0.7f); }
void sfx_cancel(void) { sound_play(snd_cancel, 0.6f); }

static bool menu_music_on;

void menu_music_start(void)
{
    if (menu_music_on && music_is_open()) return;
    const char *p[1] = {fs_path("snd/preload/music/freakyMenu/freakyMenu.ogg")};
    if (music_open(p, 1, true)) {
        music_fade(0, 0.7f, 4);
        music_play();
        menu_music_on = true;
    }
}

bool menu_music_playing(void) { return menu_music_on && music_is_open(); }

void menu_music_stop(void)
{
    music_close();
    menu_music_on = false;
}

void options_apply(void)
{
    audio_set_master(opt.music_volume / 10.0f, opt.sfx_volume / 10.0f);
    if (opt.widescreen == 1) gfx_set_widescreen(true);
    else if (opt.widescreen == 2) gfx_set_widescreen(false);
    else gfx_set_widescreen(CONF_GetAspectRatio() == CONF_ASPECT_16_9);
}

static void scene_call(scene_id s, int what, float dt)
{
    switch (s) {
#define S(id, name)                       \
    case id:                              \
        if (what == 0) name##_enter();    \
        else if (what == 1) name##_update(dt); \
        else if (what == 2) name##_draw();  \
        else name##_exit();               \
        break;
        S(SCENE_TITLE, title)
        S(SCENE_MAINMENU, mainmenu)
        S(SCENE_STORY, story)
        S(SCENE_FREEPLAY, freeplay)
        S(SCENE_OPTIONS, options)
        S(SCENE_PLAY, play)
#undef S
    default:
        break;
    }
}

static void draw_box(float x, float y, float w, float h)
{
    gfx_rect(x - 4, y - 4, w + 8, h + 8, rgba(255, 255, 255, 255));
    gfx_rect(x, y, w, h, rgba(20, 20, 30, 240));
}

/* The update notice from libwiiupdate, drawn over any scene. */
static void draw_update_popup(const wiiupdate_popup *pop)
{
    gfx_set_camera(NULL);
    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, 140));
    float bw = 820, bh = 260, bx = (GAME_W - bw) / 2, by = (GAME_H - bh) / 2;
    draw_box(bx, by, bw, bh);
    text_draw(pop->title, GAME_W / 2, by + 24, 40, 0xffffffff, ALIGN_CENTER);
    if (pop->text) text_draw(pop->text, GAME_W / 2, by + 86, 24, 0xffffffff, ALIGN_CENTER);
    if (pop->detail && pop->detail[0]) text_draw(pop->detail, GAME_W / 2, by + 122, 18, 0xbbbbbbff, ALIGN_CENTER);
    if (pop->progress >= 0) {
        gfx_rect(bx + 60, by + 160, bw - 120, 24, rgba(60, 60, 60, 255));
        gfx_rect(bx + 60, by + 160, (bw - 120) * pop->progress / 100.0f, 24, rgba(102, 255, 51, 255));
    }
    if (pop->hint && pop->hint[0]) text_draw(pop->hint, GAME_W / 2, by + bh - 48, 24, 0xffff66ff, ALIGN_CENTER);
}

static void missing_data_screen(void)
{
    for (;;) {
        in_update();
        if (in_pressed() & (ACT_HOME | ACT_ACCEPT | ACT_BACK) || power_off || reset_pressed) break;
        gfx_begin();
        gfx_rect(0, 0, GAME_W, GAME_H, rgba(20, 0, 30, 255));
        text_draw("FNFWii " APP_VERSION, GAME_W / 2, 140, 48, 0xffffffff, ALIGN_CENTER);
        text_draw("The game data was not found.", GAME_W / 2, 260, 32, 0xff8080ff, ALIGN_CENTER);
        text_draw("Build it on a computer from your copy of the game's assets:\n\n"
                  "  python3 tools/convert.py Funkin.assets data\n\n"
                  "and copy the data folder next to boot.dol\n"
                  "(sd:/apps/FNFWii/data).",
                  GAME_W / 2, 330, 24, 0xffffffff, ALIGN_CENTER);
        text_draw("Press A or HOME to exit.", GAME_W / 2, 600, 24, 0xffff66ff, ALIGN_CENTER);
        gfx_end();
    }
}

int main(int argc, char **argv)
{
    gfx_init();
    in_init();
    fatInitDefault();
    SYS_SetPowerCallback(on_power);
    SYS_SetResetCallback(on_reset);
    WPAD_SetPowerButtonCallback(on_wpad_power);
    audio_init();

    bool have_data = fs_init(argc > 0 ? argv[0] : NULL);
    log_open();
    LOG("FNFWii %s, root %s", APP_VERSION, fs_root());
    autotest_init();
    text_init();
    save_load();
    options_apply();

    WIIUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    static const char *const update_files[] = {"boot.dol", "meta.xml", "icon.png", NULL};
    WIIUPDATE_SetUpdateFiles(update_files);
    if (opt.check_updates && !autotest_active()) WIIUPDATE_Start("quatric", APP_NAME, APP_VERSION);

    if (!have_data) {
        missing_data_screen();
        goto out;
    }

    snd_scroll = sound_load(fs_path("snd/preload/sounds/scrollMenu.ogg"));
    snd_confirm = sound_load(fs_path("snd/preload/sounds/confirmMenu.ogg"));
    snd_cancel = sound_load(fs_path("snd/preload/sounds/cancelMenu.ogg"));

    current = autotest_first_scene(SCENE_TITLE);
    scene_call(current, 0, 0);
    fade = 1;
    fade_dir = -1;

    double last = now_ms();
    while (current != SCENE_QUIT) {
        double t = now_ms();
        float dt = (float)((t - last) / 1000.0);
        last = t;
        if (dt > 0.1f) dt = 0.1f;
        if (dt < 0) dt = 0;
        elapsed_total += dt;

        in_update();
        autotest_input();
        if (power_off || reset_pressed) break;

        audio_update(dt);
        gfx_begin();
        /* the update popup takes the controls while it is open */
        wiiupdate_popup pop;
        int pst = WIIUPDATE_Popup(in_raw_down(), &pop);
        bool popup_open = pst == WIIUPDATE_POPUP_OPEN;
        if (pst == WIIUPDATE_POPUP_EXIT) game_switch(SCENE_QUIT);
        if (!popup_open && fade_dir <= 0 && pending == SCENE_NONE) scene_call(current, 1, dt);
        else if (!popup_open) scene_call(current, 1, 0);
        scene_call(current, 2, 0);
        if (popup_open) draw_update_popup(&pop);

        if (fade_dir != 0) {
            fade += fade_dir * dt / 0.35f;
            if (fade <= 0) {
                fade = 0;
                fade_dir = 0;
            }
            if (fade >= 1 && fade_dir > 0) {
                fade = 1;
                scene_call(current, 3, 0);
                current = pending;
                pending = SCENE_NONE;
                atlas_purge();
                if (current != SCENE_QUIT) {
                    gfx_set_camera(NULL);
                    gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, 255));
                    scene_call(current, 0, 0);
                    last = now_ms();
                    fade_dir = -1;
                }
            }
        }
        if (fade > 0) {
            gfx_set_camera(NULL);
            gfx_rect(0, 0, GAME_W, GAME_H, rgba(0, 0, 0, (int)(fade * 255)));
        }
        autotest_frame();
        gfx_end();
    }
    save_write();

out:
    audio_shutdown();
    INPUT_Shutdown();
    if (power_off) SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    return 0;
}
