/* GX renderer: everything is drawn on a virtual 1280x720 canvas, the
 * resolution the game was laid out for. On a 16:9 console it fills the
 * screen (anamorphic); on 4:3 it is letterboxed. */
#ifndef GFX_H
#define GFX_H

#include <gccore.h>
#include <stdbool.h>

#define GAME_W 1280
#define GAME_H 720

typedef struct {
    GXTexObj obj;
    void *data;
    int w, h;          /* texture size in pixels */
    bool loaded;
} texture;

typedef struct {
    float x, y;        /* scroll: the world point at the top-left of the view */
    float zoom;
    float angle;       /* degrees */
    float shake_x, shake_y;
} camera;

void gfx_init(void);
void gfx_begin(void);
void gfx_end(void);
bool gfx_widescreen(void);
void gfx_set_widescreen(bool on);
u32  gfx_frame(void);           /* frames drawn so far */
float gfx_refresh_rate(void);   /* 50 or 60 */

/* Textures in the converter's .tex format (GX tiled data with a small header). */
bool tex_load(texture *t, const char *path, bool nearest);
bool tex_load_mem(texture *t, const void *buf, int size, bool nearest);
void tex_free(texture *t);
void tex_set_nearest(texture *t, bool nearest);
texture *tex_white(void);

/* Sets the camera every following draw goes through (NULL = screen space). */
void gfx_set_camera(const camera *cam);

/* Draws texture region (u,v,uw,vh in texture pixels) to the quad
 * (x,y,w,h) in canvas/world coordinates, rotated by angle degrees around
 * (ox,oy) (relative to the quad corner). flip_x/flip_y mirror the source. */
typedef struct {
    texture *tex;
    float u, v, uw, vh;
    float x, y, w, h;
    float angle, ox, oy;
    bool flip_x, flip_y;
    u32 color;          /* RGBA, multiplied */
} gfx_quad;

void gfx_draw(const gfx_quad *q);
/* Quad from four corners (clockwise from top-left) with texture coords 0..1. */
void gfx_draw_pts(texture *t, const float pts[8], const float uv[8], u32 rgba);
void gfx_rect(float x, float y, float w, float h, u32 rgba);
void gfx_set_additive(bool on);

/* Saves the next finished frame's XFB (raw YUYV, fbWidth x xfbHeight) to path. */
void gfx_request_dump(const char *path);
int gfx_fb_width(void);
int gfx_fb_height(void);
/* Screenshot to a 24-bit BMP (debug builds). */
bool gfx_screenshot(const char *path);

static inline u32 rgba(int r, int g, int b, int a) { return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | (u32)a; }
static inline u32 rgba_alpha(u32 c, float a)
{
    int al = (int)((c & 0xff) * a);
    if (al < 0) al = 0;
    if (al > 255) al = 255;
    return (c & 0xffffff00) | (u32)al;
}

#endif
