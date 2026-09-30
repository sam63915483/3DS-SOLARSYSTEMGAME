#include "target.h"
#include "render.h"
#include "font.h"
#include <stdio.h>

#define SCREEN_W 400.0f
#define SCREEN_H 240.0f
#define CROSSHAIR_RADIUS 70.0f   /* px from the centre within which a body is a candidate */

void target_init(target_state *t) { t->candidate = -1; t->locked = -1; }

/* Apparent radius of a body in pixels from the camera. */
static float apparent_radius(const world *w, int i, const camera *c) {
    float d = v3_len(v3_sub(w->b[i].pos, c->pos));
    float r = w->b[i].radius + w->b[i].atmo_h * 0.5f;
    if (d <= r) return SCREEN_H;
    /* vertical fov 55 degrees: half the screen height covers tan(27.5) of depth */
    return (r / d) / 0.5206f * (SCREEN_H * 0.5f);
}

void target_update(target_state *t, const world *w, const player *p, const camera *c, int toggle) {
    if (player_on_foot(p)) { t->candidate = -1; t->locked = -1; return; }
    t->candidate = -1;
    float best = CROSSHAIR_RADIUS;
    for (int i = 0; i < BODY_COUNT; i++) {
        if (i == p->dom && p->altitude < w->b[i].atmo_h) continue;   /* not the one we are standing on */
        float sx, sy;
        if (!render_project(w->b[i].pos, &sx, &sy)) continue;
        float dx = sx - SCREEN_W / 2, dy = sy - SCREEN_H / 2;
        float dist = sqrtf(dx * dx + dy * dy) - apparent_radius(w, i, c) * 0.5f;
        if (dist < best) { best = dist; t->candidate = i; }
    }
    if (toggle) {
        if (t->locked >= 0) t->locked = -1;
        else if (t->candidate >= 0) t->locked = t->candidate;
    }
}

static void line(float x0, float y0, float x1, float y1, float th, u32 col) {
    C2D_DrawLine(x0, y0, col, x1, y1, col, th, 0);
}

static void ring(float cx, float cy, float r, float th, u32 col) {
    const int n = 40;
    float px = cx + r, py = cy;
    for (int k = 1; k <= n; k++) {
        float a = 6.2831853f * k / n;
        float x = cx + r * cosf(a), y = cy + r * sinf(a);
        line(px, py, x, y, th, col);
        px = x; py = y;
    }
}

static void brackets(float cx, float cy, float half, float len, float th, u32 col) {
    float l = cx - half, r = cx + half, t = cy - half, b = cy + half;
    line(l, t, l + len, t, th, col); line(l, t, l, t + len, th, col);
    line(r, t, r - len, t, th, col); line(r, t, r, t + len, th, col);
    line(l, b, l + len, b, th, col); line(l, b, l, b - len, th, col);
    line(r, b, r - len, b, th, col); line(r, b, r, b - len, th, col);
}

static void arrow(float x0, float y0, float x1, float y1, float th, u32 col) {
    line(x0, y0, x1, y1, th, col);
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1) return;
    dx /= l; dy /= l;
    line(x1, y1, x1 - dx * 8 + dy * 5, y1 - dy * 8 - dx * 5, th, col);
    line(x1, y1, x1 - dx * 8 - dy * 5, y1 - dy * 8 + dx * 5, th, col);
}

void target_draw(C3D_RenderTarget *top, const target_state *t, const world *w, const player *p, const camera *c) {
    if (player_on_foot(p)) return;
    C2D_SceneBegin(top);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    const u32 orange = C2D_Color32(255, 170, 60, 230), white = C2D_Color32(240, 245, 255, 230);
    const u32 dimc = C2D_Color32(240, 245, 255, 120);

    /* crosshair */
    line(SCREEN_W / 2 - 6, SCREEN_H / 2, SCREEN_W / 2 + 6, SCREEN_H / 2, 1, dimc);
    line(SCREEN_W / 2, SCREEN_H / 2 - 6, SCREEN_W / 2, SCREEN_H / 2 + 6, 1, dimc);

    if (t->locked < 0 && t->candidate >= 0) {
        float sx, sy;
        if (render_project(w->b[t->candidate].pos, &sx, &sy)) {
            float half = apparent_radius(w, t->candidate, c) + 8;
            if (half < 16) half = 16;
            if (half > 150) half = 150;
            brackets(sx, sy, half, half * 0.4f, 1.5f, orange);
            font_draw("DPAD UP: LOCK", sx - font_width("DPAD UP: LOCK", 1) / 2, sy + half + 6, 1, orange);
        }
        C2D_Flush();
        return;
    }
    if (t->locked < 0) { C2D_Flush(); return; }

    const body *b = &w->b[t->locked];
    float sx, sy;
    int visible = render_project(b->pos, &sx, &sy);
    float r = apparent_radius(w, t->locked, c) + 8;
    if (r < 16) r = 16;
    if (r > 150) r = 150;

    /* relative motion */
    v3 v_world = v3_add(p->vel, w->b[p->dom].vel);
    v3 v_rel = v3_sub(v_world, b->vel);
    v3 to = v3_sub(b->pos, p->pos);
    float dist = v3_len(to);
    v3 dir = v3_scale(to, 1.0f / dist);
    float closing = v3_dot(v_rel, dir);
    v3 lateral = v3_sub(v_rel, v3_scale(dir, closing));
    v3 cam_right = v3_cross(c->view, c->view_up);
    float lx = v3_dot(lateral, cam_right), ly = -v3_dot(lateral, c->view_up);
    float lat = sqrtf(lx * lx + ly * ly);

    if (visible) {
        ring(sx, sy, r, 1.5f, white);
        /* drift arrow: where you are sliding relative to the target, longer = faster */
        if (lat > 0.3f) {
            float len = 12 + lat * 2.5f;
            if (len > 90) len = 90;
            arrow(sx, sy, sx + lx / lat * len, sy + ly / lat * len, 2, orange);
        }
        char buf[64];
        font_draw(b->name, sx - font_width(b->name, 1) / 2, sy - r - 12, 1, white);
        snprintf(buf, sizeof buf, "DIST %.0f", dist - b->radius);
        font_draw(buf, sx - font_width(buf, 1) / 2, sy + r + 6, 1, white);
        snprintf(buf, sizeof buf, "%s %.1f", closing > 0 ? "CLOSING" : "RECEDING", fabsf(closing));
        font_draw(buf, sx - font_width(buf, 1) / 2, sy + r + 16, 1, closing > 0 ? orange : white);
        /* closing gauge: a bar under the readout, filled toward the middle when approaching */
        float g = closing / 40.0f; if (g > 1) g = 1; if (g < -1) g = -1;
        line(sx - 40, sy + r + 30, sx + 40, sy + r + 30, 1, dimc);
        line(sx, sy + r + 30, sx + g * 40, sy + r + 30, 3, closing > 0 ? orange : white);
    } else {
        /* off screen: arrow at the edge pointing the way */
        v3 rel = to;
        float ex = v3_dot(rel, cam_right), ey = -v3_dot(rel, c->view_up);
        float l = sqrtf(ex * ex + ey * ey);
        if (l > 1e-3f) {
            ex /= l; ey /= l;
            float cx = SCREEN_W / 2 + ex * 170, cy = SCREEN_H / 2 + ey * 95;
            arrow(cx - ex * 16, cy - ey * 16, cx, cy, 2, orange);
            font_draw(b->name, cx - font_width(b->name, 1) / 2 - ex * 20, cy - ey * 20 - 4, 1, orange);
        }
    }
    C2D_Flush();
}
