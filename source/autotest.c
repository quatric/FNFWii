#include "autotest.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    float t;
    char cmd[12];
    char arg[48];
    bool done;
} at_event;

static at_event events[64];
static int nevents;
static bool active;
static scene_id first = SCENE_NONE;
static double start_ms;
static u32 inject;
static FILE *state_log;

bool autotest_active(void) { return active; }

void autotest_init(void)
{
    FILE *f = fopen(fs_app_path("autotest.txt"), "r");
    if (!f) return;
    active = true;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char a[32] = "", b[48] = "", c[48] = "", d[48] = "";
        int n = sscanf(line, "%31s %47s %47s %47s", a, b, c, d);
        if (n <= 0) continue;
        if (!strcmp(a, "play") && n >= 2) {
            first = SCENE_PLAY;
            memset(&playreq, 0, sizeof(playreq));
            strncpy(playreq.songs[0], b, 47);
            playreq.nsongs = 1;
            strncpy(playreq.difficulty, n >= 3 ? c : "normal", 15);
            strncpy(playreq.variation, n >= 4 ? d : "default", 15);
        } else if (!strcmp(a, "scene") && n >= 2) {
            if (!strcmp(b, "title")) first = SCENE_TITLE;
            else if (!strcmp(b, "mainmenu")) first = SCENE_MAINMENU;
            else if (!strcmp(b, "story")) first = SCENE_STORY;
            else if (!strcmp(b, "freeplay")) first = SCENE_FREEPLAY;
            else if (!strcmp(b, "options")) first = SCENE_OPTIONS;
        } else if (!strcmp(a, "botplay")) {
            opt.botplay = true;
        } else if (nevents < 64 && n >= 2) {
            at_event *e = &events[nevents++];
            e->t = atof(b);
            strncpy(e->cmd, a, sizeof(e->cmd) - 1);
            strncpy(e->arg, c, sizeof(e->arg) - 1);
        }
    }
    fclose(f);
    mkdir(fs_app_path("shots"), 0777);
    state_log = fopen(fs_app_path("shots/state.txt"), "w");
    start_ms = now_ms();
}

scene_id autotest_first_scene(scene_id def) { return first != SCENE_NONE ? first : def; }

void autotest_log_state(const char *fmt, ...)
{
    if (!state_log) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(state_log, "%8.2f ", (now_ms() - start_ms) / 1000.0);
    vfprintf(state_log, fmt, ap);
    va_end(ap);
    fputc('\n', state_log);
    fflush(state_log);
    fsync(fileno(state_log));
}

static u32 action(const char *s)
{
    if (!strcmp(s, "accept")) return ACT_ACCEPT;
    if (!strcmp(s, "back")) return ACT_BACK;
    if (!strcmp(s, "pause")) return ACT_PAUSE;
    if (!strcmp(s, "left")) return ACT_LEFT;
    if (!strcmp(s, "down")) return ACT_DOWN;
    if (!strcmp(s, "up")) return ACT_UP;
    if (!strcmp(s, "right")) return ACT_RIGHT;
    return 0;
}

extern void in_inject(u32 actions);

void autotest_input(void)
{
    if (!active) return;
    float t = (float)((now_ms() - start_ms) / 1000.0);
    inject = 0;
    for (int i = 0; i < nevents; i++) {
        at_event *e = &events[i];
        if (e->done || t < e->t) continue;
        if (!strcmp(e->cmd, "press")) {
            inject |= action(e->arg);
            e->done = true;
        } else if (!strcmp(e->cmd, "seek") && game_scene() == SCENE_PLAY && play_debug_seek(atof(e->arg))) {
            autotest_log_state("seek %s", e->arg);
            e->done = true;
        }
    }
    if (inject) in_inject(inject);
}

void autotest_frame(void)
{
    if (!active) return;
    float t = (float)((now_ms() - start_ms) / 1000.0);
    static int last_tick = -1;
    if ((int)t != last_tick) {
        last_tick = (int)t;
        autotest_log_state("tick scene %d music %.0f/%.0f ms open %d", game_scene(), music_time_ms(), music_length_ms(),
                           music_is_open());
    }
    for (int i = 0; i < nevents; i++) {
        at_event *e = &events[i];
        if (e->done || t < e->t) continue;
        if (!strcmp(e->cmd, "shot")) {
            char rel[96];
            snprintf(rel, sizeof(rel), "shots/%s.yuv", e->arg[0] ? e->arg : "shot");
            gfx_request_dump(fs_app_path(rel));
            autotest_log_state("shot %s", rel);
            e->done = true;
            break; /* one per frame */
        } else if (!strcmp(e->cmd, "quit")) {
            e->done = true;
            autotest_log_state("quit");
            if (state_log) fclose(state_log);
            state_log = NULL;
            FILE *f = fopen(fs_app_path("autotest.done"), "w");
            if (f) fclose(f);
            game_switch(SCENE_QUIT);
        }
    }
}
