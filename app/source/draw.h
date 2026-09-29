/* citro2d helpers in native screen pixels. Text sizes are given in pixels like the mockup's CSS. */
#ifndef DRAW_H
#define DRAW_H
#include <3ds.h>
#include <citro2d.h>

enum { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT };

#define RGB(r, g, b) C2D_Color32((r), (g), (b), 0xff)
#define RGBA(r, g, b, a) C2D_Color32((r), (g), (b), (a))

void  draw_init(void);
void  draw_frame_begin(void);          /* clears the per-frame text buffer */
void  draw_rect(float x, float y, float w, float h, u32 color);
void  draw_rect_outline(float x, float y, float w, float h, float thickness, u32 color);
void  draw_box(float x, float y, float w, float h, u32 fill, u32 border, float thickness);
void  draw_line(float x0, float y0, float x1, float y1, float thickness, u32 color);
void  draw_circle(float x, float y, float r, u32 color);
/* y is the vertical centre of the text line. px is the font size in pixels. */
void  draw_text(const char *s, float x, float y, float px, u32 color, int align);
float draw_text_width(const char *s, float px);

#endif
