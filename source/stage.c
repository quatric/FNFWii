#include "stage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static bool ends(const char *s, const char *e)
{
    size_t a = strlen(s), b = strlen(e);
    return a >= b && !strcmp(s + a - b, e);
}

/* ------------------------------------------------------------ characters */

bool char_has(const character *c, const char *name) { return sprite_has_anim(&c->spr, name); }

static void find_counts(character *c, const char *prefix, int *out, int *n)
{
    *n = 0;
    size_t pl = strlen(prefix);
    for (int i = 0; i < c->spr.nanims && *n < 8; i++) {
        const char *nm = c->spr.anims[i].name;
        if (!strncmp(nm, prefix, pl) && nm[pl] >= '0' && nm[pl] <= '9') out[(*n)++] = atoi(nm + pl);
    }
    /* ascending */
    for (int i = 0; i < *n; i++)
        for (int j = i + 1; j < *n; j++)
            if (out[j] < out[i]) {
                int t = out[i];
                out[i] = out[j];
                out[j] = t;
            }
}

bool char_load(character *c, const char *id, char_type type)
{
    memset(c, 0, sizeof(*c));
    char rel[96];
    snprintf(rel, sizeof(rel), "chars/%s.json", id);
    cJSON *j = fs_json(rel);
    if (!j) {
        LOG("character missing: %s", id);
        return false;
    }
    strncpy(c->id, id, sizeof(c->id) - 1);
    c->type = type;
    sprite_init(&c->spr);
    c->spr.use_anim_offsets = true;
    sprite_add_anims_json(&c->spr, cJSON_GetObjectItem(j, "anims"), NULL);
    cJSON *off = cJSON_GetObjectItem(j, "offsets");
    c->spr.global_off_x = jidx(off, 0, 0);
    c->spr.global_off_y = jidx(off, 1, 0);
    cJSON *co = cJSON_GetObjectItem(j, "cameraOffsets");
    c->data_cam_off[0] = jidx(co, 0, 0);
    c->data_cam_off[1] = jidx(co, 1, 0);
    c->base_scale = jnum(j, "scale", 1);
    c->sing_steps = jnum(j, "singTime", 8);
    c->dance_every = jnum(j, "danceEvery", 1);
    c->data_flip = jbool(j, "flipX", false);
    strncpy(c->icon, jstr(j, "icon", ""), sizeof(c->icon) - 1);
    c->icon_scale = jnum(j, "iconScale", 1);
    c->icon_pixel = jbool(j, "iconPixel", false);
    c->icon_flip = jbool(j, "iconFlipX", false);
    cJSON *dco = cJSON_GetObjectItem(j, "deathCameraOffsets");
    c->death_cam_off[0] = jidx(dco, 0, 0);
    c->death_cam_off[1] = jidx(dco, 1, 0);
    c->death_zoom = jnum(j, "deathCameraZoom", 1);
    c->alternate = char_has(c, "danceLeft");
    find_counts(c, "combo", c->combo_counts, &c->ncombo);
    find_counts(c, "drop", c->drop_counts, &c->ndrop);
    const char *start = jstr(j, "startingAnimation", "idle");
    cJSON_Delete(j);

    c->spr.scale_x = c->spr.scale_y = c->base_scale;
    c->loaded = true;
    char_dance(c, true);
    if (c->spr.cur < 0 && start) sprite_play(&c->spr, start, true);
    if (c->spr.cur < 0 && c->spr.nanims > 0) sprite_play_index(&c->spr, 0, true, 0);
    sprite_update_hitbox(&c->spr);
    return true;
}

void char_free(character *c)
{
    if (c->loaded) sprite_free(&c->spr);
    c->loaded = false;
}

bool char_is_singing(const character *c)
{
    const char *a = sprite_anim_name(&c->spr);
    return starts(a, "sing") && !ends(a, "-end");
}

static const char *correct_name(const character *c, const char *name, char *buf, size_t n)
{
    strncpy(buf, name, n - 1);
    buf[n - 1] = 0;
    for (;;) {
        if (char_has(c, buf)) return buf;
        char *dash = strrchr(buf, '-');
        if (!dash) return NULL;
        *dash = 0;
    }
}

void char_play(character *c, const char *name, bool restart, bool lock)
{
    if (!c->loaded) return;
    if (c->locked) {
        if (!(restart && !strcmp(sprite_anim_name(&c->spr), name))) return;
    }
    char buf[48];
    const char *cn = correct_name(c, name, buf, sizeof(buf));
    if (!cn) return;
    sprite_play(&c->spr, cn, restart);
    if (lock) c->locked = true;
}

void char_dance(character *c, bool force)
{
    if (!c->loaded || c->dead) return;
    if (!force) {
        if (char_is_singing(c)) return;
        const char *a = sprite_anim_name(&c->spr);
        if (!starts(a, "dance") && !starts(a, "idle") && !sprite_anim_finished(&c->spr)) return;
    }
    if (c->alternate) {
        char_play(c, c->danced ? "danceRight" : "danceLeft", false, false);
        c->danced = !c->danced;
    } else {
        char_play(c, "idle", false, false);
    }
}

void char_sing(character *c, int dir, bool miss, const char *suffix)
{
    static const char *const dirs[4] = {"LEFT", "DOWN", "UP", "RIGHT"};
    char name[48];
    snprintf(name, sizeof(name), "sing%s%s%s%s", dirs[dir & 3], miss ? "miss" : "", suffix && suffix[0] ? "-" : "",
             suffix ? suffix : "");
    char_play(c, name, true, false);
}

void char_step(character *c, int step)
{
    if (!c->loaded) return;
    if (c->dance_every > 0) {
        int every = (int)roundf(c->dance_every * 4);
        if (every > 0 && step % every == 0) char_dance(c, false);
    }
}

void char_update(character *c, float dt, double step_len_ms, bool holding)
{
    if (!c->loaded) return;
    sprite_update(&c->spr, dt);
    if (c->locked && sprite_anim_finished(&c->spr)) c->locked = false;
    if (c->dead) return;
    const char *cur = sprite_anim_name(&c->spr);
    char hold[56];
    snprintf(hold, sizeof(hold), "%s-hold", cur);
    if (sprite_anim_finished(&c->spr) && !ends(cur, "-hold") && char_has(c, hold)) {
        sprite_play(&c->spr, hold, false);
        cur = sprite_anim_name(&c->spr);
    }
    if (char_is_singing(c)) {
        c->hold_timer += dt;
        float sing = c->sing_steps * (float)(step_len_ms / 1000.0);
        if (ends(cur, "miss")) sing *= 2;
        bool stop = c->type == CT_BF ? !holding : true;
        if (c->hold_timer > sing && stop) {
            c->hold_timer = 0;
            char base[56];
            strncpy(base, cur, sizeof(base) - 1);
            base[sizeof(base) - 1] = 0;
            if (ends(base, "-hold")) base[strlen(base) - 5] = 0;
            char endn[64];
            snprintf(endn, sizeof(endn), "%s-end", base);
            if (char_has(c, endn)) char_play(c, endn, false, false);
            else char_dance(c, true);
        }
    } else {
        c->hold_timer = 0;
    }
}

/* ------------------------------------------------------------ stages */

bool stage_load(stage *s, const char *id)
{
    memset(s, 0, sizeof(*s));
    char rel[96];
    snprintf(rel, sizeof(rel), "stages/%s.json", id);
    cJSON *j = fs_json(rel);
    if (!j) {
        LOG("stage missing: %s", id);
        j = fs_json("stages/mainStage.json");
        if (!j) return false;
    }
    strncpy(s->id, id, sizeof(s->id) - 1);
    s->zoom = jnum(j, "cameraZoom", 1.0);
    cJSON *props = cJSON_GetObjectItem(j, "props");
    int n = cJSON_GetArraySize(props);
    s->props = calloc(n > 0 ? n : 1, sizeof(stage_prop));
    cJSON *p;
    cJSON_ArrayForEach(p, props)
    {
        stage_prop *q = &s->props[s->nprops];
        sprite_init(&q->spr);
        strncpy(q->name, jstr(p, "name", ""), sizeof(q->name) - 1);
        cJSON *pos = cJSON_GetObjectItem(p, "position");
        cJSON *sc = cJSON_GetObjectItem(p, "scale");
        cJSON *sf = cJSON_GetObjectItem(p, "scroll");
        const char *solid = jstr(p, "solid", NULL);
        if (solid) {
            q->solid = true;
            q->color = parse_color(solid, 0xffffffff);
            q->spr.frame_w = jidx(sc, 0, 1);
            q->spr.frame_h = jidx(sc, 1, q->spr.frame_w);
            q->spr.scale_x = q->spr.scale_y = 1;
        } else {
            const char *atl = jstr(p, "atlas", NULL);
            if (!atl || !sprite_load(&q->spr, atl)) {
                sprite_free(&q->spr);
                continue;
            }
            cJSON *anims = cJSON_GetObjectItem(p, "anims");
            sprite_add_anims_json(&q->spr, anims, atl);
            q->spr.scale_x = jidx(sc, 0, 1);
            q->spr.scale_y = jidx(sc, 1, q->spr.scale_x);
            q->spr.use_anim_offsets = cJSON_GetArraySize(anims) > 0 || jnum(p, "danceEvery", 0) != 0;
            const char *col = jstr(p, "color", NULL);
            if (col) q->spr.color = parse_color(col, 0xffffffff);
            const char *blend = jstr(p, "blend", NULL);
            q->spr.additive = blend && !strcmp(blend, "add");
        }
        sprite_update_hitbox(&q->spr);
        q->spr.x = jidx(pos, 0, 0);
        q->spr.y = jidx(pos, 1, 0);
        q->spr.sf_x = jidx(sf, 0, 1);
        q->spr.sf_y = jidx(sf, 1, 1);
        q->spr.alpha = jnum(p, "alpha", 1);
        q->spr.angle = jnum(p, "angle", 0);
        q->spr.flip_x = jbool(p, "flipX", false);
        q->spr.flip_y = jbool(p, "flipY", false);
        q->z = (int)jnum(p, "zIndex", 0);
        q->danceEvery = jnum(p, "danceEvery", 0);
        q->alternate = sprite_has_anim(&q->spr, "danceLeft");
        const char *start = jstr(p, "startingAnimation", NULL);
        if (start) sprite_play(&q->spr, start, false);
        s->nprops++;
    }
    static const char *const keys[3] = {"bf", "dad", "gf"};
    cJSON *chars = cJSON_GetObjectItem(j, "characters");
    for (int i = 0; i < 3; i++) {
        cJSON *c = cJSON_GetObjectItem(chars, keys[i]);
        cJSON *pos = cJSON_GetObjectItem(c, "position");
        cJSON *co = cJSON_GetObjectItem(c, "cameraOffsets");
        cJSON *sf = cJSON_GetObjectItem(c, "scroll");
        s->pos[i].x = jidx(pos, 0, i == 0 ? 989.5 : i == 1 ? 335 : 751.5);
        s->pos[i].y = jidx(pos, 1, i == 2 ? 787 : 885);
        s->pos[i].cam_off[0] = jidx(co, 0, 0);
        s->pos[i].cam_off[1] = jidx(co, 1, 0);
        s->pos[i].sf_x = jidx(sf, 0, 1);
        s->pos[i].sf_y = jidx(sf, 1, 1);
        s->pos[i].scale = jnum(c, "scale", 1);
        s->pos[i].z = (int)jnum(c, "zIndex", i == 0 ? 300 : i == 1 ? 200 : 100);
    }
    cJSON_Delete(j);
    s->loaded = true;
    return true;
}

void stage_free(stage *s)
{
    for (int i = 0; i < s->nprops; i++) sprite_free(&s->props[i].spr);
    free(s->props);
    memset(s, 0, sizeof(*s));
}

void stage_place(stage *s, character *c)
{
    if (!c->loaded) return;
    int k = c->type == CT_BF ? 0 : c->type == CT_DAD ? 1 : 2;
    sprite *sp = &c->spr;
    sp->flip_x = c->type == CT_BF ? !c->data_flip : c->data_flip;
    /* resetCharacter(): idle frame, hitbox at the character's own scale */
    sp->x = sp->y = 0;
    sp->scale_x = sp->scale_y = c->base_scale;
    c->danced = false;
    char_dance(c, true);
    sprite_update_hitbox(sp);
    sp->x = s->pos[k].x - sp->width / 2;
    sp->y = s->pos[k].y - sp->height;
    /* setScale(base * stage scale) keeps the feet in place and adds the global offsets */
    float fx = sp->x + sp->width / 2, fy = sp->y + sp->height;
    float sc = c->base_scale * s->pos[k].scale;
    sp->scale_x = sp->scale_y = sc;
    sprite_update_hitbox(sp);
    sp->x = fx - sp->width / 2 + sp->global_off_x;
    sp->y = fy - sp->height + sp->global_off_y;
    c->orig_x = sp->x;
    c->orig_y = sp->y;
    c->focus_x = c->orig_x + sp->width / 2 + c->data_cam_off[0] + s->pos[k].cam_off[0];
    c->focus_y = c->orig_y + sp->height / 2 + c->data_cam_off[1] + s->pos[k].cam_off[1];
    sp->sf_x = s->pos[k].sf_x;
    sp->sf_y = s->pos[k].sf_y;
    c->z = s->pos[k].z;
}

void stage_step(stage *s, int step)
{
    for (int i = 0; i < s->nprops; i++) {
        stage_prop *q = &s->props[i];
        if (q->danceEvery <= 0) continue;
        int every = (int)roundf(q->danceEvery * 4);
        if (every <= 0 || step % every) continue;
        if (q->alternate) {
            sprite_play(&q->spr, q->danced ? "danceRight" : "danceLeft", true);
            q->danced = !q->danced;
        } else {
            sprite_play(&q->spr, "idle", true);
        }
    }
}

void stage_update(stage *s, float dt)
{
    for (int i = 0; i < s->nprops; i++) sprite_update(&s->props[i].spr, dt);
}

stage_prop *stage_find_prop(stage *s, const char *name)
{
    for (int i = 0; i < s->nprops; i++)
        if (!strcmp(s->props[i].name, name)) return &s->props[i];
    return NULL;
}

static void draw_prop(stage_prop *q, float cx, float cy)
{
    if (q->solid) {
        if (!q->spr.visible) return;
        float x = q->spr.x - cx * q->spr.sf_x, y = q->spr.y - cy * q->spr.sf_y;
        gfx_rect(x, y, q->spr.frame_w, q->spr.frame_h, rgba_alpha(q->color, q->spr.alpha));
        return;
    }
    sprite_draw(&q->spr, cx, cy);
}

void stage_draw(stage *s, character **chars, int nchars, float cx, float cy)
{
    /* insertion order for equal zIndex, like the game's sort */
    int total = s->nprops + nchars;
    int order[96];
    int zs[96];
    int n = 0;
    for (int i = 0; i < total && n < 96; i++) {
        int z;
        if (i < s->nprops) z = s->props[i].z;
        else if (chars[i - s->nprops] && chars[i - s->nprops]->loaded) z = chars[i - s->nprops]->z;
        else continue;
        int k = n++;
        while (k > 0 && zs[k - 1] > z) {
            zs[k] = zs[k - 1];
            order[k] = order[k - 1];
            k--;
        }
        zs[k] = z;
        order[k] = i;
    }
    for (int k = 0; k < n; k++) {
        int i = order[k];
        if (i < s->nprops) draw_prop(&s->props[i], cx, cy);
        else sprite_draw(&chars[i - s->nprops]->spr, cx, cy);
    }
}
