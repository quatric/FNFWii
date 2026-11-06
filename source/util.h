#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <gctypes.h>

#include "cjson/cJSON.h"

#define APP_NAME "FNFWii"
#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

/* --- files: every game path is relative to the data folder --- */
bool fs_init(const char *argv0);
const char *fs_root(void);          /* e.g. "sd:/apps/FNFWii" */
const char *fs_path(const char *rel); /* data/<rel>; returns a static buffer (rotating) */
const char *fs_app_path(const char *rel); /* <app folder>/<rel> (save file, logs) */
bool fs_exists(const char *rel);
void *fs_read(const char *rel, size_t *size); /* malloc'd, NUL-terminated */
cJSON *fs_json(const char *rel);

void log_open(void);
void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) logf_(__VA_ARGS__)

/* --- cJSON helpers --- */
double jnum(const cJSON *o, const char *k, double def);
const char *jstr(const cJSON *o, const char *k, const char *def);
bool jbool(const cJSON *o, const char *k, bool def);
double jidx(const cJSON *arr, int i, double def);

/* --- maths --- */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float ease_apply(const char *name, float t); /* FlxEase by name ("quadOut", "linear", ...) */
u32 parse_color(const char *s, u32 def);     /* "#RRGGBB" / "0xAARRGGBB" -> RGBA */
float randf(void);

/* --- time --- */
double now_ms(void);

char *xstrdup(const char *s);

#endif
