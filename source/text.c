#include <stdlib.h>
#include "text.h"

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "atlas.h"
#include "util.h"

static atlas *vcr;
static int glyph_frame[128];
static float glyph_adv[128];
static float vcr_size = 32, vcr_line = 32;

typedef struct {
    atlas *at;
    float max_h;
    bool upper_only;
    const atl_anim *anims[128];
} afont;
static afont afonts[2];

static const char *special_prefix(char c)
{
    switch (c) {
    case '&': return "-andpersand-";
    case '\'': return "-apostraphie-";
    case '\\': return "-back slash-";
    case ',': return "-comma-";
    case '-': return "-dash-";
    case '"': return "-start quote-";
    case '!': return "-exclamation point-";
    case '/': return "-forward slash-";
    case '>': return "-greater than-";
    case '<': return "-less than-";
    case '*': return "-multiply x-";
    case '.': return "-period-";
    case '?': return "-question mark-";
    default: return NULL;
    }
}

static void load_afont(afont *f, const char *path)
{
    f->at = atlas_get(path);
    if (!f->at) return;
    bool has_lower = false, has_upper = false;
    for (int c = 33; c < 127; c++) {
        char key[64];
        const char *sp = special_prefix((char)c);
        if (sp) snprintf(key, sizeof(key), "sparrow|%s|", sp);
        else snprintf(key, sizeof(key), "sparrow|%c|", c);
        f->anims[c] = atlas_anim(f->at, key);
        if (f->anims[c]) {
            if (isupper(c)) has_upper = true;
            if (islower(c)) has_lower = true;
            for (int i = 0; i < f->anims[c]->count; i++) {
                const atl_frame *fr = &f->at->frames[f->anims[c]->frames[i]];
                /* trimmed height like frame.frame.height: use the piece's extent */
                float h = fr->fh;
                if (fr->count) {
                    const atl_piece *p = &f->at->pieces[fr->first];
                    h = p->m[3] * p->vh;
                }
                if (h > f->max_h) f->max_h = h;
            }
        }
    }
    f->upper_only = has_upper && !has_lower;
}

bool text_init(void)
{
    vcr = atlas_get("font/vcr");
    cJSON *j = fs_json("font/vcr.json");
    for (int i = 0; i < 128; i++) glyph_frame[i] = -1;
    if (j) {
        vcr_size = jnum(j, "size", 32);
        vcr_line = jnum(j, "line", 32);
        cJSON *g = cJSON_GetObjectItem(j, "glyphs");
        cJSON *e;
        cJSON_ArrayForEach(e, g)
        {
            int code = atoi(e->string);
            if (code > 0 && code < 128) {
                glyph_frame[code] = (int)jidx(e, 0, -1);
                glyph_adv[code] = jidx(e, 1, 0);
            }
        }
        cJSON_Delete(j);
    }
    cJSON *ui = fs_json("ui.json");
    if (ui) {
        load_afont(&afonts[AFONT_BOLD], jstr(cJSON_GetObjectItem(ui, "fontBold"), "atlas", "img/preload/fonts/bold"));
        load_afont(&afonts[AFONT_DEFAULT], jstr(cJSON_GetObjectItem(ui, "fontDefault"), "atlas", "img/preload/fonts/default"));
        cJSON_Delete(ui);
    }
    return vcr != NULL;
}

float text_width(const char *s, float size)
{
    float k = size / vcr_size, w = 0, best = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c == '\n') {
            if (w > best) best = w;
            w = 0;
            continue;
        }
        if (c < 128) w += glyph_adv[c] * k;
    }
    return w > best ? w : best;
}

static void draw_line(const char *s, int len, float x, float y, float k, u32 color)
{
    for (int i = 0; i < len; i++) {
        unsigned c = (unsigned char)s[i];
        if (c >= 128) continue;
        if (glyph_frame[c] >= 0) {
            float m[6] = {k, 0, 0, k, x, y};
            atlas_draw_frame(vcr, glyph_frame[c], m, color);
        }
        x += glyph_adv[c] * k;
    }
}

void text_draw(const char *s, float x, float y, float size, u32 color, int align)
{
    if (!vcr) return;
    float k = size / vcr_size;
    while (*s) {
        const char *nl = strchr(s, '\n');
        int len = nl ? (int)(nl - s) : (int)strlen(s);
        float w = 0;
        for (int i = 0; i < len; i++) {
            unsigned c = (unsigned char)s[i];
            if (c < 128) w += glyph_adv[c] * k;
        }
        float lx = align == ALIGN_CENTER ? x - w / 2 : align == ALIGN_RIGHT ? x - w : x;
        draw_line(s, len, lx, y, k, color);
        y += vcr_line * k;
        if (!nl) break;
        s = nl + 1;
    }
}

void text_draw_outline(const char *s, float x, float y, float size, u32 color, u32 outline, float t, int align)
{
    static const float d[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    u32 oc = (outline & 0xffffff00) | (((outline & 0xff) * (color & 0xff)) / 255);
    for (int i = 0; i < 8; i++) text_draw(s, x + d[i][0] * t, y + d[i][1] * t, size, oc, align);
    text_draw(s, x, y, size, color, align);
}

static const atl_anim *achar(afont *f, char c)
{
    if (f->upper_only) c = toupper((unsigned char)c);
    unsigned u = (unsigned char)c;
    if (u >= 128) return NULL;
    return f->anims[u];
}

float atext_width(atlas_font which, const char *s, float scale)
{
    afont *f = &afonts[which];
    if (!f->at) return 0;
    float w = 0;
    for (; *s; s++) {
        if (*s == ' ') {
            w += 40 * scale;
            continue;
        }
        const atl_anim *a = achar(f, *s);
        if (a) w += f->at->frames[a->frames[0]].fw * scale;
    }
    return w;
}

float atext_height(atlas_font which, float scale) { return afonts[which].max_h * scale; }

void atext_draw(atlas_font which, const char *s, float x, float y, float scale, u32 color, float time)
{
    afont *f = &afonts[which];
    if (!f->at) return;
    float xp = 0;
    for (; *s; s++) {
        if (*s == ' ') {
            xp += 40 * scale;
            continue;
        }
        const atl_anim *a = achar(f, *s);
        if (!a) continue;
        int fi = a->frames[((int)(time * 24)) % a->count];
        const atl_frame *fr = &f->at->frames[a->frames[0]];
        float cy = y + (f->max_h - fr->fh) * scale;
        float m[6] = {scale, 0, 0, scale, x + xp, cy};
        atlas_draw_frame(f->at, fi, m, color);
        xp += fr->fw * scale;
    }
}
