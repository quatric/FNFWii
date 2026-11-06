/* Sprite atlases written by tools/convert.py (".atl", format FAT2).
 *
 * A frame is a list of pieces; each piece is a rectangle of a texture page
 * with a matrix placing it in the frame (in the game's own units). Sparrow
 * frames have one piece, Adobe Animate frames one per body part. */
#ifndef ATLAS_H
#define ATLAS_H

#include "gfx.h"

typedef struct {
    u8 page;
    u16 u, v, uw, vh;
    float m[6];   /* a b c d tx ty: texture px -> frame units */
    u32 color;    /* RGBA multiplier */
} atl_piece;

typedef struct {
    float fw, fh;
    u32 first;
    u16 count;
} atl_frame;

typedef struct {
    char *key;
    u16 count;
    u16 *frames;
} atl_anim;

typedef struct atlas {
    char path[128];
    int refs;
    int npages;
    texture *pages;
    int nframes;
    atl_frame *frames;
    int npieces;
    atl_piece *pieces;
    int nanims;
    atl_anim *anims;
    bool nearest;
    int fps;
    struct atlas *next;
} atlas;

/* "img/shared/characters/bf" (no extension). Cached and reference counted. */
atlas *atlas_get(const char *path);
void atlas_put(atlas *a);
/* Frees every atlas nobody holds any more. */
void atlas_purge(void);
size_t atlas_memory(void);

const atl_anim *atlas_anim(const atlas *a, const char *key);

/* xf maps frame units to canvas units (a b c d tx ty). */
void atlas_draw_frame(const atlas *a, int frame, const float xf[6], u32 color);

static inline void mat_mul(float out[6], const float p[6], const float c[6])
{
    /* out = p after c */
    float r[6];
    r[0] = p[0] * c[0] + p[2] * c[1];
    r[1] = p[1] * c[0] + p[3] * c[1];
    r[2] = p[0] * c[2] + p[2] * c[3];
    r[3] = p[1] * c[2] + p[3] * c[3];
    r[4] = p[0] * c[4] + p[2] * c[5] + p[4];
    r[5] = p[1] * c[4] + p[3] * c[5] + p[5];
    for (int i = 0; i < 6; i++) out[i] = r[i];
}

#endif
