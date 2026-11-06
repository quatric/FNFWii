#include "sprite.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void sprite_init(sprite *s)
{
    memset(s, 0, sizeof(*s));
    s->scale_x = s->scale_y = 1;
    s->sf_x = s->sf_y = 1;
    s->alpha = 1;
    s->color = 0xffffffff;
    s->visible = true;
    s->cur = -1;
}

static void reset_frame_size(sprite *s)
{
    atlas *at;
    bool flip;
    int f = sprite_cur_frame(s, &at, &flip);
    if (f >= 0) {
        s->frame_w = at->frames[f].fw;
        s->frame_h = at->frames[f].fh;
    }
    s->width = s->frame_w * fabsf(s->scale_x);
    s->height = s->frame_h * fabsf(s->scale_y);
    s->origin_x = s->frame_w / 2;
    s->origin_y = s->frame_h / 2;
}

bool sprite_load(sprite *s, const char *path)
{
    if (s->base) atlas_put(s->base);
    s->base = atlas_get(path);
    s->static_frame = 0;
    reset_frame_size(s);
    return s->base != NULL;
}

void sprite_free(sprite *s)
{
    for (int i = 0; i < s->nanims; i++) atlas_put(s->anims[i].at);
    free(s->anims);
    if (s->base) atlas_put(s->base);
    s->anims = NULL;
    s->nanims = 0;
    s->base = NULL;
    s->cur = -1;
}

bool sprite_add_anim(sprite *s, const char *name, const char *atlas_path, const char *key, float fps, bool loop,
                     float off_x, float off_y, bool flip_x)
{
    atlas *a = atlas_get(atlas_path);
    if (!a) return false;
    const atl_anim *an = atlas_anim(a, key);
    if (!an || an->count == 0) {
        atlas_put(a);
        return false;
    }
    /* a later definition with the same name replaces the earlier one */
    int idx = sprite_find_anim(s, name);
    if (idx < 0) {
        s->anims = realloc(s->anims, sizeof(spr_anim) * (s->nanims + 1));
        idx = s->nanims++;
    } else {
        atlas_put(s->anims[idx].at);
    }
    spr_anim *d = &s->anims[idx];
    memset(d, 0, sizeof(*d));
    strncpy(d->name, name, sizeof(d->name) - 1);
    d->at = a;
    d->an = an;
    d->fps = fps > 0 ? fps : 24;
    d->loop = loop;
    d->off_x = off_x;
    d->off_y = off_y;
    d->flip_x = flip_x;
    if (s->frame_w == 0) {
        s->frame_w = a->frames[an->frames[0]].fw;
        s->frame_h = a->frames[an->frames[0]].fh;
        s->width = s->frame_w * fabsf(s->scale_x);
        s->height = s->frame_h * fabsf(s->scale_y);
        s->origin_x = s->frame_w / 2;
        s->origin_y = s->frame_h / 2;
    }
    return true;
}

void sprite_add_anims_json(sprite *s, const cJSON *arr, const char *default_atlas)
{
    const cJSON *a;
    cJSON_ArrayForEach(a, arr)
    {
        const cJSON *off = cJSON_GetObjectItem(a, "offsets");
        sprite_add_anim(s, jstr(a, "name", ""), jstr(a, "atlas", default_atlas), jstr(a, "key", ""),
                        jnum(a, "fps", 24), jbool(a, "loop", false), jidx(off, 0, 0), jidx(off, 1, 0),
                        jbool(a, "flipX", false));
    }
}

int sprite_find_anim(const sprite *s, const char *name)
{
    for (int i = 0; i < s->nanims; i++)
        if (!strcmp(s->anims[i].name, name)) return i;
    return -1;
}

bool sprite_has_anim(const sprite *s, const char *name) { return sprite_find_anim(s, name) >= 0; }

void sprite_play_index(sprite *s, int idx, bool force, int start)
{
    if (idx < 0 || idx >= s->nanims) return;
    if (!force && s->cur == idx && !s->finished) {
        s->paused = false;
        return;
    }
    s->cur = idx;
    s->frame = start;
    if (s->frame >= s->anims[idx].an->count) s->frame = s->anims[idx].an->count - 1;
    s->timer = 0;
    s->finished = false;
    s->paused = false;
    s->anim_off_x = s->anims[idx].off_x;
    s->anim_off_y = s->anims[idx].off_y;
    if (s->anims[idx].an->count == 1 && !s->anims[idx].loop) s->finished = false;
}

void sprite_play(sprite *s, const char *name, bool force)
{
    sprite_play_index(s, sprite_find_anim(s, name), force, 0);
}

const char *sprite_anim_name(const sprite *s)
{
    return s->cur >= 0 ? s->anims[s->cur].name : "";
}

bool sprite_anim_finished(const sprite *s)
{
    return s->cur < 0 || s->finished;
}

void sprite_update(sprite *s, float dt)
{
    if (s->cur < 0 || s->paused) return;
    spr_anim *a = &s->anims[s->cur];
    if (s->finished) return;
    float dur = 1.0f / a->fps;
    s->timer += dt;
    int n = a->an->count;
    while (s->timer >= dur && !s->finished) {
        s->timer -= dur;
        if (s->frame + 1 >= n) {
            if (a->loop) s->frame = 0;
            else s->finished = true;
        } else {
            s->frame++;
        }
    }
}

int sprite_cur_frame(const sprite *s, atlas **at, bool *flip)
{
    if (s->cur >= 0) {
        const spr_anim *a = &s->anims[s->cur];
        int fi = s->frame < a->an->count ? s->frame : a->an->count - 1;
        if (at) *at = a->at;
        if (flip) *flip = a->flip_x;
        return a->an->frames[fi];
    }
    if (s->base && s->base->nframes > 0) {
        if (at) *at = s->base;
        if (flip) *flip = false;
        return s->static_frame < s->base->nframes ? s->static_frame : 0;
    }
    return -1;
}

float sprite_cur_fw(const sprite *s)
{
    atlas *at;
    int f = sprite_cur_frame(s, &at, NULL);
    return f >= 0 ? at->frames[f].fw : 0;
}

void sprite_update_hitbox(sprite *s)
{
    atlas *at;
    int f = sprite_cur_frame(s, &at, NULL);
    if (f >= 0) {
        s->frame_w = at->frames[f].fw;
        s->frame_h = at->frames[f].fh;
    }
    s->width = fabsf(s->scale_x) * s->frame_w;
    s->height = fabsf(s->scale_y) * s->frame_h;
    s->offset_x = -0.5f * (s->width - s->frame_w);
    s->offset_y = -0.5f * (s->height - s->frame_h);
    s->origin_x = s->frame_w / 2;
    s->origin_y = s->frame_h / 2;
}

void sprite_set_graphic_size(sprite *s, float w, float h)
{
    if (w <= 0 && h <= 0) return;
    float nx = w / s->frame_w, ny = h / s->frame_h;
    s->scale_x = w > 0 ? nx : ny;
    s->scale_y = h > 0 ? ny : nx;
}

void sprite_screen_center(sprite *s, bool cx, bool cy)
{
    if (cx) s->x = (GAME_W - s->width) / 2;
    if (cy) s->y = (GAME_H - s->height) / 2;
}

void sprite_draw(const sprite *s, float cam_x, float cam_y)
{
    if (!s->visible || s->alpha <= 0) return;
    atlas *at;
    bool aflip;
    int f = sprite_cur_frame(s, &at, &aflip);
    if (f < 0) return;
    const atl_frame *fr = &at->frames[f];
    bool fx = s->flip_x ^ aflip;

    /* FlxSprite.drawComplex: frame flip, -origin, scale, rotate, +origin, +screen point */
    float px = s->x - cam_x * s->sf_x - s->offset_x;
    float py = s->y - cam_y * s->sf_y - s->offset_y;
    if (s->use_anim_offsets) {
        px -= (s->anim_off_x - s->global_off_x) * s->scale_x;
        py -= (s->anim_off_y - s->global_off_y) * s->scale_y;
    }
    float m[6] = {1, 0, 0, 1, 0, 0};
    if (fx) {
        m[0] = -1;
        m[4] = fr->fw;
    }
    if (s->flip_y) {
        m[3] = -1;
        m[5] = fr->fh;
    }
    float t[6] = {1, 0, 0, 1, -s->origin_x, -s->origin_y};
    mat_mul(m, t, m);
    float sc[6] = {s->scale_x, 0, 0, s->scale_y, 0, 0};
    mat_mul(m, sc, m);
    if (s->angle != 0) {
        float r = s->angle * (float)M_PI / 180.0f;
        float c = cosf(r), sn = sinf(r);
        float rot[6] = {c, sn, -sn, c, 0, 0};
        mat_mul(m, rot, m);
    }
    m[4] += s->origin_x + px;
    m[5] += s->origin_y + py;
    if (s->additive) gfx_set_additive(true);
    atlas_draw_frame(at, f, m, rgba_alpha(s->color, s->alpha));
    if (s->additive) gfx_set_additive(false);
}
