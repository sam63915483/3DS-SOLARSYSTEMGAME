#include "hud.h"
#include <stdio.h>

#define FONT_CELL 30.0f   /* system font line height at scale 1.0 */

static C2D_TextBuf textBuf;

void hud_init(void) { textBuf = C2D_TextBufNew(2048); }
void hud_fini(void) { C2D_TextBufDelete(textBuf); }

static void text(const char *s, float x, float y, float px, u32 color) {
    C2D_Text t;
    if (!C2D_TextParse(&t, textBuf, s)) return;
    C2D_TextOptimize(&t);
    float scale = px / FONT_CELL;
    C2D_DrawText(&t, C2D_WithColor, x, y, 0, scale, scale, color);
}

void hud_draw(C3D_RenderTarget *bottom, const world *w, const player *p, float fps) {
    C2D_TextBufClear(textBuf);
    C2D_TargetClear(bottom, C2D_Color32(0x10, 0x12, 0x1A, 0xFF));
    C2D_SceneBegin(bottom);

    const u32 white = C2D_Color32(0xF0, 0xF0, 0xF0, 0xFF);
    const u32 dim = C2D_Color32(0x90, 0x98, 0xA8, 0xFF);
    const u32 accent = C2D_Color32(0xFF, 0xC0, 0x40, 0xFF);
    char buf[128];
    const body *b = &w->b[p->dom];

    C2D_DrawRectSolid(0, 0, 0, 320, 26, C2D_Color32(0x20, 0x24, 0x34, 0xFF));
    text("ORBIT", 8, 3, 20, accent);
    snprintf(buf, sizeof buf, "%.0f fps", fps);
    text(buf, 260, 6, 14, dim);

    snprintf(buf, sizeof buf, "%s   %s", b->name, p->grounded ? "GROUNDED" : (p->thrusting ? "THRUST" : "FLYING"));
    text(buf, 8, 34, 18, white);
    snprintf(buf, sizeof buf, "speed %5.1f   alt %6.1f   g %4.1f", p->speed, p->altitude,
             v3_len(world_gravity_at(w, p->pos)));
    text(buf, 8, 58, 15, white);

    text("distance to", 8, 86, 13, dim);
    float y = 102;
    for (int i = 0; i < BODY_COUNT; i++) {
        if (i == p->dom) continue;
        float d = v3_len(v3_sub(w->b[i].pos, p->pos)) - w->b[i].radius;
        snprintf(buf, sizeof buf, "%-8s %6.0f", w->b[i].name, d);
        text(buf, 16, y, 14, white);
        y += 17;
    }

    C2D_DrawRectSolid(0, 160, 0, 320, 1, C2D_Color32(0x30, 0x34, 0x44, 0xFF));
    if (p->grounded) {
        text("Circle pad  walk      X  jump", 8, 166, 13, dim);
        text("L / R  turn camera", 8, 182, 13, dim);
    } else {
        text("Circle pad  look      L / R  roll", 8, 166, 13, dim);
        text("X  thrust up         B  thrust down", 8, 182, 13, dim);
        text("A  boost forward     Y  brake", 8, 198, 13, dim);
    }
    text("SELECT  respawn    START  quit", 8, 214, 13, dim);
}
