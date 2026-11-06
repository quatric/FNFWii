/* Animated sprites with HaxeFlixel's placement rules (x/y, offset, origin,
 * scale, scroll factor, flip), so positions from the game's data files land
 * where they do in the original. */
#ifndef SPRITE_H
#define SPRITE_H

#include "atlas.h"
#include "util.h"

typedef struct {
    char name[40];
    atlas *at;
    const atl_anim *an;
    float fps;
    bool loop;
    float off_x, off_y;   /* Funkin animation offsets */
    bool flip_x;
} spr_anim;

typedef struct {
    float x, y;            /* world position of the corner (FlxSprite.x/y) */
    float scale_x, scale_y;
    float sf_x, sf_y;      /* scroll factor */
    float angle;
    float alpha;
    u32 color;             /* RGBA tint */
    bool flip_x, flip_y;
    bool visible;
    bool additive;
    float offset_x, offset_y;  /* FlxSprite.offset */
    float origin_x, origin_y;
    float frame_w, frame_h;    /* FlxSprite.frameWidth/Height */
    float width, height;
    float global_off_x, global_off_y; /* Bopper.globalOffsets */
    float anim_off_x, anim_off_y;     /* offsets of the playing animation */
    bool use_anim_offsets;     /* Bopper-style offsets (characters, props) */

    spr_anim *anims;
    int nanims;
    atlas *base;           /* frame source when there are no animations */
    int cur;               /* animation index or -1 */
    int frame;             /* index into the animation */
    float timer;
    bool finished;
    bool paused;
    int static_frame;      /* frame drawn with no animation */
} sprite;

void sprite_init(sprite *s);
/* A sprite showing frame 0 of an atlas (images and one-frame atlases). */
bool sprite_load(sprite *s, const char *atlas_path);
void sprite_free(sprite *s);
/* Adds an animation by atlas key; returns false if the atlas lacks it. */
bool sprite_add_anim(sprite *s, const char *name, const char *atlas_path, const char *key, float fps, bool loop,
                     float off_x, float off_y, bool flip_x);
/* Adds animations from a JSON array of {name, key, fps, loop, offsets, flipX[, atlas]}. */
void sprite_add_anims_json(sprite *s, const cJSON *arr, const char *default_atlas);
int  sprite_find_anim(const sprite *s, const char *name);
bool sprite_has_anim(const sprite *s, const char *name);
void sprite_play(sprite *s, const char *name, bool force);
void sprite_play_index(sprite *s, int idx, bool force, int start_frame);
const char *sprite_anim_name(const sprite *s);
bool sprite_anim_finished(const sprite *s);
void sprite_update(sprite *s, float dt);
void sprite_update_hitbox(sprite *s);
void sprite_set_graphic_size(sprite *s, float w, float h);
void sprite_screen_center(sprite *s, bool x, bool y);
/* The atlas frame being shown, or -1. */
int  sprite_cur_frame(const sprite *s, atlas **at, bool *flip);
float sprite_cur_fw(const sprite *s);
void sprite_draw(const sprite *s, float cam_x, float cam_y);

#endif
