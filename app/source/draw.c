#include "draw.h"

static C2D_TextBuf textBuf;
#define FONT_CELL 30.0f   /* system font line height at scale 1.0 */

void draw_init(void) { textBuf = C2D_TextBufNew(4096); }
void draw_frame_begin(void) { C2D_TextBufClear(textBuf); }

void draw_rect(float x, float y, float w, float h, u32 color) { C2D_DrawRectSolid(x, y, 0, w, h, color); }

void draw_rect_outline(float x, float y, float w, float h, float t, u32 color) {
    C2D_DrawRectSolid(x, y, 0, w, t, color);
    C2D_DrawRectSolid(x, y + h - t, 0, w, t, color);
    C2D_DrawRectSolid(x, y, 0, t, h, color);
    C2D_DrawRectSolid(x + w - t, y, 0, t, h, color);
}

void draw_box(float x, float y, float w, float h, u32 fill, u32 border, float t) {
    draw_rect(x, y, w, h, fill);
    draw_rect_outline(x, y, w, h, t, border);
}

void draw_line(float x0, float y0, float x1, float y1, float t, u32 color) {
    C2D_DrawLine(x0, y0, color, x1, y1, color, t, 0);
}

void draw_circle(float x, float y, float r, u32 color) { C2D_DrawCircleSolid(x, y, 0, r, color); }

void draw_text(const char *s, float x, float y, float px, u32 color, int align) {
    C2D_Text t;
    if (!C2D_TextParse(&t, textBuf, s)) return;
    C2D_TextOptimize(&t);
    float scale = px / FONT_CELL, w, h;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    float dx = align == ALIGN_CENTER ? -w / 2 : (align == ALIGN_RIGHT ? -w : 0);
    C2D_DrawText(&t, C2D_WithColor, x + dx, y - h / 2, 0, scale, scale, color);
}

float draw_text_width(const char *s, float px) {
    C2D_Text t;
    if (!C2D_TextParse(&t, textBuf, s)) return 0;
    float scale = px / FONT_CELL, w, h;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}
