#include "fishhud.h"
#include "font.h"
#include <math.h>

#define BAR_W 120
#define BAR_H 5
#define BAR_Y 205
#define CX 200

static float bar_a;   /* bar fade 0..1 */

static u32 rgba(int r, int g, int b, float a) { return C2D_Color32(r, g, b, (u8)(a * 255)); }
static void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.0f, w, h, c); }

void fishhud_draw(C3D_RenderTarget *top, const fishing *f, float dt) {
    C2D_SceneBegin(top);
    float goal = f->state == FISH_FIGHT ? 1.0f : 0.0f;
    bar_a += (goal - bar_a) * (1.0f - expf(-9.0f * dt));
    if (bar_a > 0.02f) {
        float ten = f->fight.tension / 100.0f;
        if (ten > 1) ten = 1;
        float hot = ten < 0.5f ? 0 : (ten - 0.5f) / 0.5f;   /* orange, reddening from half, vivid red at full */
        int r = 255, g = (int)(192 + (20 - 192) * hot), b = (int)(64 + (12 - 64) * hot);
        u32 fill = rgba(r, g, b, bar_a), brk = hot > 0.5f ? rgba(255, 40, 24, bar_a) : rgba(255, 192, 64, bar_a);
        float x0 = CX - BAR_W / 2 + sinf(f->time * 46.0f) * 3.2f * f->bar_shake, y0 = BAR_Y;
        rect(x0, y0, BAR_W, BAR_H, rgba(0, 0, 0, 0.42f * bar_a));
        rect(x0, y0, BAR_W * ten, BAR_H, fill);
        /* corner brackets */
        float l = x0 - 4, rr = x0 + BAR_W + 4, t = y0 - 4, bt = y0 + BAR_H + 4;
        rect(l, t, 7, 2, brk); rect(l, t, 2, 7, brk);
        rect(rr - 7, t, 7, 2, brk); rect(rr - 2, t, 2, 7, brk);
        rect(l, bt - 2, 7, 2, brk); rect(l, bt - 7, 2, 7, brk);
        rect(rr - 7, bt - 2, 7, 2, brk); rect(rr - 2, bt - 7, 2, 7, brk);
    }
    if (f->state == FISH_STRIKE && fmodf(f->time, 0.25f) < 0.125f) {
        const char *s = "HOOK!  TAP R";
        font_draw(s, CX - font_width(s, 2) / 2, BAR_Y - 4, 2, rgba(255, 192, 64, 1));
    }
    if (f->lost_t > 0) {   /* solid for a second, then fades over the next */
        float a = f->lost_t > 1.0f ? 1.0f : f->lost_t;
        font_draw(f->lost_msg, CX - font_width(f->lost_msg, 1.5f) / 2, BAR_Y - 22, 1.5f, rgba(255, 60, 40, a));
    }
    if (f->banner_t > 0) {
        float a = f->banner_t < 0.5f ? f->banner_t / 0.5f : 1.0f;
        font_draw(f->banner, CX - font_width(f->banner, 2) / 2, 60, 2, rgba(255, 192, 64, a));
    }
    C2D_Flush();
}
