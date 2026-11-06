/* Characters and stages, placed the way the game's Stage/BaseCharacter do. */
#ifndef STAGE_H
#define STAGE_H

#include "sprite.h"

typedef enum { CT_BF, CT_DAD, CT_GF, CT_OTHER } char_type;

typedef struct {
    bool loaded;
    sprite spr;
    char id[48];
    char_type type;
    float sing_steps;
    float dance_every;
    bool alternate, danced;
    float hold_timer;
    bool dead;
    bool locked;           /* playAnimation(..., ignoreOther) */
    float base_scale;
    float data_cam_off[2]; /* character JSON cameraOffsets */
    float focus_x, focus_y;
    float orig_x, orig_y;
    bool data_flip;
    char icon[128];
    float icon_scale;
    bool icon_pixel, icon_flip;
    float death_cam_off[2];
    float death_zoom;
    int combo_counts[8], ncombo;
    int drop_counts[8], ndrop;
    int z;
} character;

bool char_load(character *c, const char *id, char_type type);
void char_free(character *c);
void char_update(character *c, float dt, double step_len_ms, bool holding);
void char_dance(character *c, bool force);
void char_play(character *c, const char *name, bool restart, bool lock);
void char_sing(character *c, int dir, bool miss, const char *suffix);
bool char_is_singing(const character *c);
void char_step(character *c, int step);
bool char_has(const character *c, const char *name);

typedef struct {
    sprite spr;
    bool solid;
    u32 color;
    float danceEvery;
    bool alternate, danced;
    int z;
    char name[40];
} stage_prop;

typedef struct {
    bool loaded;
    char id[48];
    float zoom;
    stage_prop *props;
    int nprops;
    struct {
        float x, y, scale, sf_x, sf_y;
        float cam_off[2];
        int z;
    } pos[3]; /* bf, dad, gf */
} stage;

bool stage_load(stage *s, const char *id);
void stage_free(stage *s);
/* Positions a character from the stage data (Stage.addCharacter). */
void stage_place(stage *s, character *c);
void stage_step(stage *s, int step);
void stage_update(stage *s, float dt);
stage_prop *stage_find_prop(stage *s, const char *name);
/* Draws props and the characters in zIndex order. */
void stage_draw(stage *s, character **chars, int nchars, float cam_x, float cam_y);

#endif
