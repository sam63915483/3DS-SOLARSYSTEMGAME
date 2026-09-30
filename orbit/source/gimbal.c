/* Navball instrument in the bottom-left of the top screen (design A of orbit/mockups).
   The ball shows where the dominant body is relative to the camera: a planet-coloured
   segment whose horizon chord tilts with roll and slides with pitch. Jets light up around it. */
#include "gimbal.h"
#include "font.h"
#include <math.h>

#define OX 4
#define OY 146

static u32 rgba(u32 rgb, float a) {
    return C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (u8)(a * 255));
}
#define DARK   0x20243A
#define SKY    0x5C7CC8
#define PLANET 0x4E8A3A
#define ORANGE 0xFFC040
#define WHITE  0xF0F0F0
#define GREY   0x9098A8

static void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.0f, w, h, c); }
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c) {
    C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, 0.0f);
}
/* thick line as two triangles */
static void line(float x0, float y0, float x1, float y1, float w, u32 c) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    float nx = -dy / len * w / 2, ny = dx / len * w / 2;
    tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}
/* isosceles arrow head centred on (px,py) pointing along ang (screen radians, y down) */
static void arrow(float px, float py, float ang, float size, u32 c) {
    float cx = cosf(ang), cy = sinf(ang), nx = -cy, ny = cx;
    tri(px + cx * size, py + cy * size,
        px - cx * size * 0.6f + nx * size * 0.7f, py - cy * size * 0.6f + ny * size * 0.7f,
        px - cx * size * 0.6f - nx * size * 0.7f, py - cy * size * 0.6f - ny * size * 0.7f, c);
}
static void keycap(float x, float y, float w, float h, const char *label, int on) {
    rect(x, y, w, h, on ? rgba(ORANGE, 1) : rgba(GREY, 0.6f));
    rect(x + 1, y + 1, w - 2, h - 2, on ? rgba(ORANGE, 1) : rgba(DARK, 0.85f));
    font_draw(label, x + (w - font_width(label, 1)) / 2, y + 2, 1, on ? rgba(DARK, 1) : rgba(GREY, 1));
}

void gimbal_draw(C3D_RenderTarget *top, const world *w, const player *p, const camera *c) {
    C2D_SceneBegin(top);

    /* direction to the dominant body in the camera's frame: x right, y up, z forward */
    v3 to = v3_norm(v3_sub(w->b[p->dom].pos, p->pos));
    v3 right = v3_norm(v3_cross(c->view, c->view_up));
    float dxl = v3_dot(to, right), dyl = v3_dot(to, c->view_up), t = v3_dot(to, c->view);
    float hl = sqrtf(dxl * dxl + dyl * dyl);

    const float cx = OX + 45, cy = OY + 40, R = 30;
    C2D_DrawCircleSolid(cx, cy, 0.0f, R + 3, rgba(DARK, 0.85f));   /* rim */
    C2D_DrawCircleSolid(cx, cy, 0.0f, R, rgba(SKY, 1));            /* sky, planet drawn over */

    if (t > 0.999f) {
        C2D_DrawCircleSolid(cx, cy, 0.0f, R, rgba(PLANET, 1));     /* staring straight at it */
    } else if (t > -0.999f && hl > 1e-4f) {
        float dx = dxl / hl, dy = -dyl / hl;              /* screen direction of "down" */
        float aD = atan2f(dy, dx);
        float alpha = acosf(-t);                          /* half-angle of the segment's arc */
        float hx = cx - dx * R * t, hy = cy - dy * R * t; /* midpoint of the horizon chord */
        const int N = 12;
        for (int i = 0; i < N; i++) {
            float a0 = aD - alpha + 2 * alpha * i / N, a1 = aD - alpha + 2 * alpha * (i + 1) / N;
            tri(hx, hy, cx + R * cosf(a0), cy + R * sinf(a0), cx + R * cosf(a1), cy + R * sinf(a1), rgba(PLANET, 1));
        }
        float hh = R * sqrtf(1 - t * t);                  /* half chord */
        float px = -dy, py = dx;                          /* along the horizon */
        line(hx + px * hh, hy + py * hh, hx - px * hh, hy - py * hh, 2, rgba(WHITE, 1));
        for (int k = -1; k <= 1; k += 2) {                /* pitch ticks for about +/-27 degrees */
            float tk = t + 0.45f * k;
            if (tk > -0.9f && tk < 0.9f) {
                float kx = cx - dx * R * tk, ky = cy - dy * R * tk;
                line(kx + px * 6, ky + py * 6, kx - px * 6, ky - py * 6, 1, rgba(WHITE, 0.6f));
            }
        }
    }

    /* notch at the top of the rim */
    arrow(cx, cy - (R - 3), -1.5707963f, 3, rgba(WHITE, 1));

    /* fixed reticle: orange wings, centre lights for forward thrust */
    rect(cx - 13, cy - 1, 9, 2, rgba(ORANGE, 1));
    rect(cx + 4, cy - 1, 9, 2, rgba(ORANGE, 1));
    rect(cx - 3, cy - 3, 6, 6, rgba(WHITE, 1));
    rect(cx - 2, cy - 2, 4, 4, p->jet_fwd ? rgba(ORANGE, 1) : rgba(DARK, 1));

    /* jets around the ball: lit orange, idle dim */
    int roll_r = p->jet_roll > 0.05f, roll_l = p->jet_roll < -0.05f, brake = v3_len(p->brake_dir) > 0.5f;
#define LIT(on) ((on) ? rgba(ORANGE, 1) : rgba(GREY, 0.45f))
    tri(cx, OY + 2, cx - 5, OY + 9, cx + 5, OY + 9, LIT(p->jet_up));
    tri(cx, OY + 78, cx - 5, OY + 71, cx + 5, OY + 71, LIT(p->jet_down));
    const float rr = R + 7, q = 0.78539816f;
    arrow(cx + rr * cosf(-q), cy + rr * sinf(-q), q, 5, LIT(roll_r));           /* clockwise */
    arrow(cx + rr * cosf(-3 * q), cy + rr * sinf(-3 * q), 3 * q, 5, LIT(roll_l)); /* anticlockwise */
#undef LIT

    keycap(OX + 6, OY + 78, 21, 11, "FWD", p->jet_fwd);
    keycap(OX + 53, OY + 78, 33, 11, "BRAKE", brake);
}
