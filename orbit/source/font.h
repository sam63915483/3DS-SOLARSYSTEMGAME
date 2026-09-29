/* Built-in 5x7 pixel font drawn with citro2d, so text works without the system font. */
#ifndef FONT_H
#define FONT_H
#include <citro2d.h>

void font_init(void);
void font_fini(void);
/* Draws s at x,y (top-left). Each glyph is 6*scale wide and 8*scale tall. */
void font_draw(const char *s, float x, float y, float scale, u32 color);
float font_width(const char *s, float scale);
/* Debug: draws the whole glyph atlas. */
void font_debug_atlas(float x, float y, float scale);

#endif
