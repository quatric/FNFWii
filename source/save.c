/* Options and high scores, kept in <app>/save.json. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

static cJSON *save;

static cJSON *section(const char *name)
{
    cJSON *s = cJSON_GetObjectItem(save, name);
    if (!s) s = cJSON_AddObjectToObject(save, name);
    return s;
}

void save_load(void)
{
    FILE *f = fopen(fs_app_path("save.json"), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = malloc(n + 1);
        if (buf) {
            buf[fread(buf, 1, n, f)] = 0;
            save = cJSON_Parse(buf);
            free(buf);
        }
        fclose(f);
    }
    if (!save) save = cJSON_CreateObject();
    cJSON *o = section("options");
    opt.downscroll = jbool(o, "downscroll", opt.downscroll);
    opt.ghost_tapping = jbool(o, "ghostTapping", opt.ghost_tapping);
    opt.flashing = jbool(o, "flashingLights", opt.flashing);
    opt.camera_zoom = jbool(o, "zoomCamera", opt.camera_zoom);
    opt.middlescroll = jbool(o, "middlescroll", opt.middlescroll);
    opt.offset_ms = (int)jnum(o, "offset", opt.offset_ms);
    opt.music_volume = (int)jnum(o, "musicVolume", opt.music_volume);
    opt.sfx_volume = (int)jnum(o, "sfxVolume", opt.sfx_volume);
    opt.widescreen = (int)jnum(o, "widescreen", opt.widescreen);
    opt.check_updates = jbool(o, "checkUpdates", opt.check_updates);
}

static void set_num(cJSON *o, const char *k, double v)
{
    cJSON *it = cJSON_GetObjectItem(o, k);
    if (it) cJSON_SetNumberValue(it, v);
    else cJSON_AddNumberToObject(o, k, v);
}

static void set_bool(cJSON *o, const char *k, bool v)
{
    cJSON_DeleteItemFromObject(o, k);
    cJSON_AddBoolToObject(o, k, v);
}

void save_write(void)
{
    if (!save) return;
    cJSON *o = section("options");
    set_bool(o, "downscroll", opt.downscroll);
    set_bool(o, "ghostTapping", opt.ghost_tapping);
    set_bool(o, "flashingLights", opt.flashing);
    set_bool(o, "zoomCamera", opt.camera_zoom);
    set_bool(o, "middlescroll", opt.middlescroll);
    set_num(o, "offset", opt.offset_ms);
    set_num(o, "musicVolume", opt.music_volume);
    set_num(o, "sfxVolume", opt.sfx_volume);
    set_num(o, "widescreen", opt.widescreen);
    set_bool(o, "checkUpdates", opt.check_updates);
    char *txt = cJSON_Print(save);
    if (!txt) return;
    FILE *f = fopen(fs_app_path("save.json.tmp"), "wb");
    if (f) {
        fwrite(txt, 1, strlen(txt), f);
        fclose(f);
        remove(fs_app_path("save.json"));
        rename(fs_app_path("save.json.tmp"), fs_app_path("save.json"));
    }
    free(txt);
}

static void score_key(char *buf, size_t n, const char *song, const char *diff, const char *var)
{
    if (!var || !var[0] || !strcmp(var, "default")) snprintf(buf, n, "%s:%s", song, diff);
    else snprintf(buf, n, "%s:%s:%s", song, diff, var);
}

int save_score(const char *song, const char *diff, const char *var)
{
    char k[128];
    score_key(k, sizeof(k), song, diff, var);
    cJSON *e = cJSON_GetObjectItem(section("scores"), k);
    return (int)jidx(e, 0, 0);
}

void save_set_score(const char *song, const char *diff, const char *var, int score, float acc)
{
    char k[128];
    score_key(k, sizeof(k), song, diff, var);
    cJSON *s = section("scores");
    cJSON *e = cJSON_GetObjectItem(s, k);
    if (e && jidx(e, 0, 0) >= score) return;
    cJSON_DeleteItemFromObject(s, k);
    e = cJSON_AddArrayToObject(s, k);
    cJSON_AddItemToArray(e, cJSON_CreateNumber(score));
    cJSON_AddItemToArray(e, cJSON_CreateNumber(acc));
    save_write();
}

int save_week_score(const char *week, const char *diff)
{
    char k[96];
    snprintf(k, sizeof(k), "%s:%s", week, diff);
    return (int)jnum(section("weeks"), k, 0);
}

void save_set_week_score(const char *week, const char *diff, int score)
{
    if (score <= save_week_score(week, diff)) return;
    char k[96];
    snprintf(k, sizeof(k), "%s:%s", week, diff);
    set_num(section("weeks"), k, score);
    save_write();
}
