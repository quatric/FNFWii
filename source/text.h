/* Text: the VCR OSD Mono bitmap font (HUD, small print) and the game's
 * animated sprite fonts (menus; AtlasText in the original). */
#ifndef TEXT_H
#define TEXT_H

#include "gfx.h"
#include <stdbool.h>

enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

bool text_init(void);
/* size is the font size in game pixels (the HUD uses 16..32). */
float text_width(const char *s, float size);
void text_draw(const char *s, float x, float y, float size, u32 color, int align);
/* With a black outline like FlxTextBorderStyle.OUTLINE. */
void text_draw_outline(const char *s, float x, float y, float size, u32 color, u32 outline, float thick, int align);

typedef enum { AFONT_BOLD, AFONT_DEFAULT } atlas_font;
/* Sprite-font text; 'time' drives the letters' wiggle. scale 1 = original size. */
float atext_width(atlas_font f, const char *s, float scale);
float atext_height(atlas_font f, float scale);
void atext_draw(atlas_font f, const char *s, float x, float y, float scale, u32 color, float time);

#endif
