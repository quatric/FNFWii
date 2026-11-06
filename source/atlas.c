#include "atlas.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static atlas *cache;

static inline u16 rd16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static inline u32 rd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static inline float rdf(const u8 *p)
{
    u32 v = rd32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void atlas_free(atlas *a)
{
    for (int i = 0; i < a->npages; i++) tex_free(&a->pages[i]);
    free(a->pages);
    free(a->frames);
    free(a->pieces);
    for (int i = 0; i < a->nanims; i++) {
        free(a->anims[i].key);
        free(a->anims[i].frames);
    }
    free(a->anims);
    free(a);
}

static atlas *atlas_load(const char *path)
{
    char rel[200];
    snprintf(rel, sizeof(rel), "%s.atl", path);
    size_t size;
    u8 *buf = fs_read(rel, &size);
    if (!buf) {
        LOG("atlas missing: %s", rel);
        return NULL;
    }
    if (size < 16 || memcmp(buf, "FAT2", 4) != 0) {
        free(buf);
        return NULL;
    }
    atlas *a = calloc(1, sizeof(*a));
    strncpy(a->path, path, sizeof(a->path) - 1);
    a->npages = rd16(buf + 4);
    a->nframes = rd16(buf + 6);
    a->npieces = rd32(buf + 8);
    a->nanims = rd16(buf + 12);
    a->nearest = buf[14];
    a->fps = buf[15] ? buf[15] : 24;
    const u8 *p = buf + 16;
    const u8 *end = buf + size;

    char dir[200];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = 0;
    else dir[0] = 0;

    a->pages = calloc(a->npages ? a->npages : 1, sizeof(texture));
    for (int i = 0; i < a->npages; i++) {
        char name[65];
        memcpy(name, p, 64);
        name[64] = 0;
        p += 64;
        char tp[300];
        snprintf(tp, sizeof(tp), "%s%s", dir, name);
        if (!tex_load(&a->pages[i], fs_path(tp), a->nearest)) LOG("texture missing: %s", tp);
    }
    a->pieces = malloc(sizeof(atl_piece) * (a->npieces ? a->npieces : 1));
    for (int i = 0; i < a->npieces && p + 38 <= end; i++, p += 38) {
        atl_piece *q = &a->pieces[i];
        q->page = p[0];
        q->u = rd16(p + 2);
        q->v = rd16(p + 4);
        q->uw = rd16(p + 6);
        q->vh = rd16(p + 8);
        for (int k = 0; k < 6; k++) q->m[k] = rdf(p + 10 + k * 4);
        q->color = rd32(p + 34);
    }
    a->frames = malloc(sizeof(atl_frame) * (a->nframes ? a->nframes : 1));
    for (int i = 0; i < a->nframes && p + 16 <= end; i++, p += 16) {
        atl_frame *f = &a->frames[i];
        f->fw = rdf(p);
        f->fh = rdf(p + 4);
        f->first = rd32(p + 8);
        f->count = rd16(p + 12);
    }
    a->anims = calloc(a->nanims ? a->nanims : 1, sizeof(atl_anim));
    for (int i = 0; i < a->nanims && p + 2 <= end; i++) {
        int kl = rd16(p);
        p += 2;
        a->anims[i].key = malloc(kl + 1);
        memcpy(a->anims[i].key, p, kl);
        a->anims[i].key[kl] = 0;
        p += kl;
        int n = rd16(p);
        p += 2;
        a->anims[i].count = n;
        a->anims[i].frames = malloc(sizeof(u16) * (n ? n : 1));
        for (int k = 0; k < n; k++) a->anims[i].frames[k] = rd16(p + k * 2);
        p += n * 2;
    }
    free(buf);
    return a;
}

atlas *atlas_get(const char *path)
{
    if (!path || !path[0]) return NULL;
    for (atlas *a = cache; a; a = a->next)
        if (!strcmp(a->path, path)) {
            a->refs++;
            return a;
        }
    atlas *a = atlas_load(path);
    if (!a) return NULL;
    a->refs = 1;
    a->next = cache;
    cache = a;
    return a;
}

void atlas_put(atlas *a)
{
    if (a && a->refs > 0) a->refs--;
}

void atlas_purge(void)
{
    atlas **pp = &cache;
    while (*pp) {
        atlas *a = *pp;
        if (a->refs <= 0) {
            *pp = a->next;
            atlas_free(a);
        } else {
            pp = &a->next;
        }
    }
}

size_t atlas_memory(void)
{
    size_t n = 0;
    for (atlas *a = cache; a; a = a->next)
        for (int i = 0; i < a->npages; i++) n += a->pages[i].w * a->pages[i].h * 2;
    return n;
}

const atl_anim *atlas_anim(const atlas *a, const char *key)
{
    if (!a || !key) return NULL;
    for (int i = 0; i < a->nanims; i++)
        if (!strcmp(a->anims[i].key, key)) return &a->anims[i];
    return NULL;
}

static inline u32 mul_color(u32 a, u32 b)
{
    if (a == 0xffffffff) return b;
    if (b == 0xffffffff) return a;
    u32 r = ((a >> 24) * (b >> 24) + 127) / 255;
    u32 g = (((a >> 16) & 0xff) * ((b >> 16) & 0xff) + 127) / 255;
    u32 bl = (((a >> 8) & 0xff) * ((b >> 8) & 0xff) + 127) / 255;
    u32 al = ((a & 0xff) * (b & 0xff) + 127) / 255;
    return r << 24 | g << 16 | bl << 8 | al;
}

void atlas_draw_frame(const atlas *a, int frame, const float xf[6], u32 color)
{
    if (!a || frame < 0 || frame >= a->nframes) return;
    const atl_frame *f = &a->frames[frame];
    for (int i = 0; i < f->count; i++) {
        const atl_piece *q = &a->pieces[f->first + i];
        if (q->page >= a->npages) continue;
        texture *t = &a->pages[q->page];
        float m[6];
        mat_mul(m, xf, q->m);
        float w = q->uw, h = q->vh;
        float pts[8] = {
            m[4], m[5],
            m[0] * w + m[4], m[1] * w + m[5],
            m[0] * w + m[2] * h + m[4], m[1] * w + m[3] * h + m[5],
            m[2] * h + m[4], m[3] * h + m[5],
        };
        float u0 = (float)q->u / t->w, v0 = (float)q->v / t->h;
        float u1 = (float)(q->u + q->uw) / t->w, v1 = (float)(q->v + q->vh) / t->h;
        float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
        gfx_draw_pts(t, pts, uv, mul_color(q->color, color));
    }
}
