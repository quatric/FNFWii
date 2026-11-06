#include "gfx.h"

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIFO_SIZE (512 * 1024)

static GXRModeObj *rmode;
static void *xfb[2];
static int cur_fb;
static void *gp_fifo;
static bool widescreen;
static u32 frames;
static Mtx44 proj;
static texture white_tex;
static GXTexObj *bound;
static int vp_x, vp_y, vp_w, vp_h;

bool gfx_widescreen(void) { return widescreen; }
u32 gfx_frame(void) { return frames; }

float gfx_refresh_rate(void)
{
    u32 tv = VIDEO_GetCurrentTvMode();
    if (tv == VI_PAL && rmode && rmode->viTVMode != VI_TVMODE_EURGB60_INT && rmode->viTVMode != VI_TVMODE_EURGB60_PROG)
        return 50.0f;
    return 60.0f;
}

static void setup_viewport(void)
{
    if (widescreen) {
        vp_x = 0;
        vp_y = 0;
        vp_w = rmode->fbWidth;
        vp_h = rmode->efbHeight;
    } else {
        /* 16:9 inside 4:3: the image keeps its shape with bars above and below. */
        vp_w = rmode->fbWidth;
        vp_h = (rmode->efbHeight * 3) / 4;
        vp_x = 0;
        vp_y = (rmode->efbHeight - vp_h) / 2;
    }
    GX_SetViewport(vp_x, vp_y, vp_w, vp_h, 0, 1);
    GX_SetScissor(vp_x, vp_y, vp_w, vp_h);
}

void gfx_set_widescreen(bool on)
{
    widescreen = on;
    if (rmode) setup_viewport();
}

static void make_white(void)
{
    /* 4x4 RGBA8 is one 64-byte tile. */
    white_tex.data = memalign(32, 64);
    memset(white_tex.data, 0xff, 64);
    DCFlushRange(white_tex.data, 64);
    GX_InitTexObj(&white_tex.obj, white_tex.data, 4, 4, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&white_tex.obj, GX_NEAR, GX_NEAR, 0, 0, 0, GX_DISABLE, GX_DISABLE, GX_ANISO_1);
    white_tex.w = white_tex.h = 4;
    white_tex.loaded = true;
}

texture *tex_white(void) { return &white_tex; }

void gfx_init(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    if (widescreen) {
        rmode->viWidth = 678;
        rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - 678) / 2;
    }
    VIDEO_Configure(rmode);
    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    gp_fifo = memalign(32, FIFO_SIZE);
    memset(gp_fifo, 0, FIFO_SIZE);
    GX_Init(gp_fifo, FIFO_SIZE);
    GXColor bg = {0, 0, 0, 0xff};
    GX_SetCopyClear(bg, GX_MAX_Z24);

    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfb_height = GX_SetDispCopyYScale(yscale);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_CopyDisp(xfb[cur_fb], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);

    guOrtho(proj, 0, GAME_H, 0, GAME_W, 0, 1);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    gfx_set_camera(NULL);
    setup_viewport();
    make_white();
}

void gfx_begin(void)
{
    bound = NULL;
    GX_InvVtxCache();
    GX_InvalidateTexAll();
    setup_viewport();
    gfx_set_camera(NULL);
    gfx_set_additive(false);
}

static char dump_path[256];

void gfx_request_dump(const char *path)
{
    strncpy(dump_path, path, sizeof(dump_path) - 1);
}

int gfx_fb_width(void) { return rmode->fbWidth; }
int gfx_fb_height(void) { return rmode->xfbHeight; }

void gfx_end(void)
{
    GX_DrawDone();
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(xfb[cur_fb], GX_TRUE);
    GX_Flush();
    if (dump_path[0]) {
        GX_DrawDone();
        FILE *f = fopen(dump_path, "wb");
        if (f) {
            u32 n = rmode->fbWidth * rmode->xfbHeight * 2;
            DCInvalidateRange(MEM_K1_TO_K0(xfb[cur_fb]), n);
            fwrite(xfb[cur_fb], 1, n, f);
            fclose(f);
        }
        dump_path[0] = 0;
    }
    VIDEO_SetNextFramebuffer(xfb[cur_fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    cur_fb ^= 1;
    frames++;
}

void gfx_set_additive(bool on)
{
    /* Textures hold premultiplied colour (tools/convert.py), so blending is
     * ONE / INV_SRC_ALPHA, and vertex colours are premultiplied too. */
    if (on) GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
    else GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

void gfx_set_camera(const camera *cam)
{
    Mtx m, t;
    guMtxIdentity(m);
    if (cam) {
        /* zoom and rotate around the centre of the canvas, like FlxCamera */
        guMtxTrans(m, -GAME_W / 2.0f + cam->shake_x, -GAME_H / 2.0f + cam->shake_y, 0);
        guMtxScale(t, cam->zoom, cam->zoom, 1);
        guMtxConcat(t, m, m);
        if (cam->angle != 0) {
            guVector axis = {0, 0, 1};
            guMtxRotAxisDeg(t, &axis, cam->angle);
            guMtxConcat(t, m, m);
        }
        guMtxTrans(t, GAME_W / 2.0f, GAME_H / 2.0f, 0);
        guMtxConcat(t, m, m);
    }
    GX_LoadPosMtxImm(m, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
}

/* --- textures --- */

typedef struct {
    char magic[4]; /* FTX1 */
    u16 w, h;
    u8 fmt, pad[3];
    u32 size;
    u8 reserved[16];
} tex_header;

static void tex_init_obj(texture *t, u8 fmt, bool nearest)
{
    GX_InitTexObj(&t->obj, t->data, t->w, t->h, fmt, GX_CLAMP, GX_CLAMP, GX_FALSE);
    tex_set_nearest(t, nearest);
}

void tex_set_nearest(texture *t, bool nearest)
{
    GX_InitTexObjLOD(&t->obj, nearest ? GX_NEAR : GX_LINEAR, nearest ? GX_NEAR : GX_LINEAR, 0, 0, 0, GX_DISABLE,
                     GX_DISABLE, GX_ANISO_1);
}

bool tex_load_mem(texture *t, const void *buf, int size, bool nearest)
{
    memset(t, 0, sizeof(*t));
    if (size < (int)sizeof(tex_header)) return false;
    const tex_header *h = buf;
    if (memcmp(h->magic, "FTX1", 4) != 0) return false;
    if ((int)(h->size + sizeof(tex_header)) > size) return false;
    t->w = h->w;
    t->h = h->h;
    t->data = memalign(32, h->size);
    if (!t->data) return false;
    memcpy(t->data, (const u8 *)buf + sizeof(tex_header), h->size);
    DCFlushRange(t->data, h->size);
    tex_init_obj(t, h->fmt, nearest);
    t->loaded = true;
    return true;
}

bool tex_load(texture *t, const char *path, bool nearest)
{
    memset(t, 0, sizeof(*t));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    tex_header h;
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, "FTX1", 4) != 0) {
        fclose(f);
        return false;
    }
    t->w = h.w;
    t->h = h.h;
    t->data = memalign(32, (h.size + 31) & ~31);
    if (!t->data) {
        fclose(f);
        return false;
    }
    size_t got = fread(t->data, 1, h.size, f);
    fclose(f);
    if (got != h.size) {
        free(t->data);
        t->data = NULL;
        return false;
    }
    DCFlushRange(t->data, h.size);
    tex_init_obj(t, h.fmt, nearest);
    t->loaded = true;
    return true;
}

void tex_free(texture *t)
{
    if (!t) return;
    if (t->data) {
        GX_DrawDone();
        free(t->data);
    }
    if (bound == &t->obj) bound = NULL;
    memset(t, 0, sizeof(*t));
}

static inline void bind(texture *t)
{
    if (bound != &t->obj) {
        GX_LoadTexObj(&t->obj, GX_TEXMAP0);
        bound = &t->obj;
    }
}

static inline u32 premul(u32 c)
{
    u32 a = c & 0xff;
    if (a == 0xff) return c;
    u32 r = ((c >> 24) * a + 127) / 255, g = (((c >> 16) & 0xff) * a + 127) / 255, b = (((c >> 8) & 0xff) * a + 127) / 255;
    return r << 24 | g << 16 | b << 8 | a;
}

void gfx_draw_pts(texture *t, const float pts[8], const float uv[8], u32 color)
{
    if (!t) t = &white_tex;
    if (!t->loaded || (color & 0xff) == 0) return;
    bind(t);
    u32 col = premul(color);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; i++) {
        GX_Position2f32(pts[i * 2], pts[i * 2 + 1]);
        GX_Color1u32(col);
        GX_TexCoord2f32(uv[i * 2], uv[i * 2 + 1]);
    }
    GX_End();
}

void gfx_draw(const gfx_quad *q)
{
    texture *t = q->tex ? q->tex : &white_tex;
    if (!t->loaded) return;
    bind(t);
    float u0 = q->u / t->w, v0 = q->v / t->h;
    float u1 = (q->u + q->uw) / t->w, v1 = (q->v + q->vh) / t->h;
    if (!q->tex) {
        u0 = v0 = 0;
        u1 = v1 = 1;
    }
    if (q->flip_x) {
        float s = u0;
        u0 = u1;
        u1 = s;
    }
    if (q->flip_y) {
        float s = v0;
        v0 = v1;
        v1 = s;
    }
    float px[4] = {0, q->w, q->w, 0};
    float py[4] = {0, 0, q->h, q->h};
    float uu[4] = {u0, u1, u1, u0};
    float vv[4] = {v0, v0, v1, v1};
    if (q->angle != 0) {
        float r = q->angle * (float)M_PI / 180.0f;
        float c = cosf(r), s = sinf(r);
        for (int i = 0; i < 4; i++) {
            float dx = px[i] - q->ox, dy = py[i] - q->oy;
            px[i] = q->ox + dx * c - dy * s;
            py[i] = q->oy + dx * s + dy * c;
        }
    }
    u32 col = premul(q->color);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; i++) {
        GX_Position2f32(q->x + px[i], q->y + py[i]);
        GX_Color1u32(col);
        GX_TexCoord2f32(uu[i], vv[i]);
    }
    GX_End();
}

void gfx_rect(float x, float y, float w, float h, u32 color)
{
    gfx_quad q = {0};
    q.x = x;
    q.y = y;
    q.w = w;
    q.h = h;
    q.color = color;
    gfx_draw(&q);
}

/* Reads the EFB back after the frame is drawn (before gfx_end copies it). */
bool gfx_screenshot(const char *path)
{
    int w = rmode->fbWidth, h = rmode->efbHeight;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    int row = (w * 3 + 3) & ~3;
    u32 size = 54 + row * h;
    u8 hdr[54] = {'B', 'M'};
    #define LE32(o, v) do { hdr[o] = (v) & 0xff; hdr[o + 1] = ((v) >> 8) & 0xff; hdr[o + 2] = ((v) >> 16) & 0xff; hdr[o + 3] = ((v) >> 24) & 0xff; } while (0)
    LE32(2, size);
    LE32(10, 54);
    LE32(14, 40);
    LE32(18, (u32)w);
    LE32(22, (u32)h);
    hdr[26] = 1;
    hdr[28] = 24;
    LE32(34, (u32)(row * h));
    #undef LE32
    fwrite(hdr, 1, 54, f);
    u8 *line = malloc(row);
    GX_DrawDone();
    for (int y = h - 1; y >= 0; y--) {
        memset(line, 0, row);
        for (int x = 0; x < w; x++) {
            GXColor c;
            GX_PeekARGB(x, y, &c);
            line[x * 3 + 0] = c.b;
            line[x * 3 + 1] = c.g;
            line[x * 3 + 2] = c.r;
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    return true;
}
