#include "util.h"

#include <math.h>
#include <ogc/lwp_watchdog.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];
static FILE *logfile;

static bool dir_has_data(const char *dir)
{
    char p[300];
    snprintf(p, sizeof(p), "%s/data/version.txt", dir);
    struct stat st;
    return stat(p, &st) == 0;
}

bool fs_init(const char *argv0)
{
    root[0] = 0;
    if (argv0 && strchr(argv0, '/')) {
        strncpy(root, argv0, sizeof(root) - 1);
        char *s = strrchr(root, '/');
        if (s) *s = 0;
        /* "sd:/apps/x/boot.dol" -> "sd:/apps/x"; some loaders pass "sd:apps/..." */
        if (dir_has_data(root)) return true;
    }
    static const char *const tries[] = {"sd:/apps/" APP_NAME, "usb:/apps/" APP_NAME, "sd:/" APP_NAME, "usb:/" APP_NAME};
    for (unsigned i = 0; i < sizeof(tries) / sizeof(tries[0]); i++) {
        if (dir_has_data(tries[i])) {
            strcpy(root, tries[i]);
            return true;
        }
    }
    if (!root[0]) strcpy(root, "sd:/apps/" APP_NAME);
    return false;
}

const char *fs_root(void) { return root; }

const char *fs_path(const char *rel)
{
    static char bufs[8][320];
    static int n;
    char *b = bufs[n++ & 7];
    snprintf(b, 320, "%s/data/%s", root, rel);
    return b;
}

const char *fs_app_path(const char *rel)
{
    static char bufs[4][320];
    static int n;
    char *b = bufs[n++ & 3];
    snprintf(b, 320, "%s/%s", root, rel);
    return b;
}

bool fs_exists(const char *rel)
{
    struct stat st;
    return stat(fs_path(rel), &st) == 0;
}

void *fs_read(const char *rel, size_t *size)
{
    FILE *f = fopen(fs_path(rel), "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc(n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    buf[got] = 0;
    if (size) *size = got;
    return buf;
}

cJSON *fs_json(const char *rel)
{
    char *t = fs_read(rel, NULL);
    if (!t) return NULL;
    cJSON *j = cJSON_Parse(t);
    free(t);
    if (!j) LOG("json parse failed: %s", rel);
    return j;
}

void log_open(void)
{
#ifndef FNF_DEBUG
    /* release builds log only during an autotest run */
    struct stat st;
    if (stat(fs_app_path("autotest.txt"), &st) != 0) return;
#endif
    logfile = fopen(fs_app_path("log.txt"), "w");
}

void logf_(const char *fmt, ...)
{
    va_list ap;
    if (logfile) {
        va_start(ap, fmt);
        vfprintf(logfile, fmt, ap);
        va_end(ap);
        fputc('\n', logfile);
        fflush(logfile);
        fsync(fileno(logfile)); /* survives a hang or a crash */
    }
}

double jnum(const cJSON *o, const char *k, double def)
{
    const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

const char *jstr(const cJSON *o, const char *k, const char *def)
{
    const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    return cJSON_IsString(v) ? v->valuestring : def;
}

bool jbool(const cJSON *o, const char *k, bool def)
{
    const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    return def;
}

double jidx(const cJSON *arr, int i, double def)
{
    const cJSON *v = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, i) : NULL;
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

/* FlxEase */
static float bounce_out(float t)
{
    if (t < 1 / 2.75f) return 7.5625f * t * t;
    if (t < 2 / 2.75f) {
        t -= 1.5f / 2.75f;
        return 7.5625f * t * t + .75f;
    }
    if (t < 2.5f / 2.75f) {
        t -= 2.25f / 2.75f;
        return 7.5625f * t * t + .9375f;
    }
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + .984375f;
}

static float ease_in(const char *base, float t)
{
    if (!strcmp(base, "quad")) return t * t;
    if (!strcmp(base, "cube")) return t * t * t;
    if (!strcmp(base, "quart")) return t * t * t * t;
    if (!strcmp(base, "quint")) return t * t * t * t * t;
    if (!strcmp(base, "sine")) return 1 - cosf(t * (float)M_PI / 2);
    if (!strcmp(base, "expo")) return t == 0 ? 0 : powf(2, 10 * (t - 1));
    if (!strcmp(base, "circ")) return 1 - sqrtf(1 - t * t);
    if (!strcmp(base, "back")) return t * t * (2.70158f * t - 1.70158f);
    if (!strcmp(base, "elastic"))
        return t == 0 || t == 1 ? t : -powf(2, 10 * (t - 1)) * sinf((t - 1 - 0.075f) * (2 * (float)M_PI) / 0.3f);
    if (!strcmp(base, "bounce")) return 1 - bounce_out(1 - t);
    if (!strcmp(base, "smoothStep")) return t * t * (3 - 2 * t);
    return t;
}

float ease_apply(const char *name, float t)
{
    t = clampf(t, 0, 1);
    if (!name || !strcmp(name, "linear")) return t;
    char base[24];
    size_t n = strlen(name);
    const char *dir = "";
    if (n > 5 && !strcmp(name + n - 5, "InOut")) {
        dir = "InOut";
        n -= 5;
    } else if (n > 3 && !strcmp(name + n - 3, "Out")) {
        dir = "Out";
        n -= 3;
    } else if (n > 2 && !strcmp(name + n - 2, "In")) {
        dir = "In";
        n -= 2;
    }
    if (n >= sizeof(base)) n = sizeof(base) - 1;
    memcpy(base, name, n);
    base[n] = 0;
    if (!strcmp(dir, "In")) return ease_in(base, t);
    if (!strcmp(dir, "Out")) return 1 - ease_in(base, 1 - t);
    if (!strcmp(dir, "InOut")) return t < .5f ? ease_in(base, t * 2) / 2 : 1 - ease_in(base, (1 - t) * 2) / 2;
    return ease_in(base, t);
}

u32 parse_color(const char *s, u32 def)
{
    if (!s) return def;
    if (s[0] == '#') {
        unsigned v = strtoul(s + 1, NULL, 16);
        if (strlen(s + 1) == 8) return (v << 8) | (v >> 24); /* #AARRGGBB */
        return (v << 8) | 0xff;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        unsigned v = strtoul(s + 2, NULL, 16);
        if (strlen(s + 2) == 8) return (v << 8) | (v >> 24);
        return (v << 8) | 0xff;
    }
    return def;
}

float randf(void) { return (rand() & 0xffff) / 65536.0f; }

double now_ms(void) { return ticks_to_microsecs(gettime()) / 1000.0; }

char *xstrdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    memcpy(d, s, n);
    return d;
}
