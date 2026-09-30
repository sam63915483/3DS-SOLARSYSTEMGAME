#include "hud.h"
#include "font.h"
#include <stdio.h>

#define BTN_X 236
#define BTN_Y 4
#define BTN_W 80
#define BTN_H 22
#define GYRO_W 64
#define GYRO_X (BTN_X - 6 - GYRO_W)

void hud_init(void) { font_init(); }
void hud_fini(void) { font_fini(); }

int hud_view_button_hit(int x, int y) {
    return x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

int hud_gyro_button_hit(int x, int y, const camera *c) {
    return c->first_person && x >= GYRO_X && x < GYRO_X + GYRO_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

static void button(int x, int w, const char *label, int lit, u32 text) {
    C2D_DrawRectSolid(x, BTN_Y, 0, w, BTN_H, lit ? C2D_Color32(0xFF, 0xC0, 0x40, 0xFF) : C2D_Color32(0x40, 0x48, 0x60, 0xFF));
    C2D_DrawRectSolid(x + 1, BTN_Y + 1, 0, w - 2, BTN_H - 2, lit ? C2D_Color32(0x5A, 0x48, 0x20, 0xFF) : C2D_Color32(0x2A, 0x30, 0x44, 0xFF));
    font_draw(label, x + (w - font_width(label, 1)) / 2, BTN_Y + 7, 1, text);
}

void hud_draw(C3D_RenderTarget *bottom, const world *w, const player *p, const camera *c, float fps, int gyro_settling) {
    C2D_TargetClear(bottom, C2D_Color32(0x10, 0x12, 0x1A, 0xFF));
    C2D_SceneBegin(bottom);

    const u32 white = C2D_Color32(0xF0, 0xF0, 0xF0, 0xFF);
    const u32 dim = C2D_Color32(0x90, 0x98, 0xA8, 0xFF);
    const u32 accent = C2D_Color32(0xFF, 0xC0, 0x40, 0xFF);
    char buf[64];
    const body *b = &w->b[p->dom];

    /* title bar with the view button */
    C2D_DrawRectSolid(0, 0, 0, 320, 30, C2D_Color32(0x20, 0x24, 0x34, 0xFF));
    font_draw("ORBIT", 8, 7, 2, accent);
    snprintf(buf, sizeof buf, "%.0f FPS", fps);
    font_draw(buf, 100, 11, 1, dim);
    button(BTN_X, BTN_W, c->first_person ? "VIEW: 1ST" : "VIEW: 3RD", 0, white);
    /* the gyro button pops up beside it in first person */
    if (c->first_person) button(GYRO_X, GYRO_W, c->gyro ? (gyro_settling ? "HOLD STILL" : "GYRO ON") : "GYRO", c->gyro, white);

    /* status */
    snprintf(buf, sizeof buf, "%s  %s", b->name, p->swimming ? "SWIMMING" : (p->grounded ? (p->in_water ? "WADING" : "GROUNDED") : (p->thrusting ? "THRUST" : "FLYING")));
    font_draw(buf, 8, 40, 2, white);
    snprintf(buf, sizeof buf, "SPD %.1f  ALT %.1f  G %.1f", p->speed, p->altitude, v3_len(world_gravity_at(w, p->pos)));
    font_draw(buf, 8, 62, 2, white);

    font_draw("DISTANCE TO", 8, 90, 1, dim);
    float y = 102;
    for (int i = 0; i < BODY_COUNT; i++) {
        if (i == p->dom) continue;
        float d = v3_len(v3_sub(w->b[i].pos, p->pos)) - w->b[i].radius;
        snprintf(buf, sizeof buf, "%-8s %6.0f", w->b[i].name, d);
        font_draw(buf, 16, y, 1, white);
        y += 11;
    }

    /* controls */
    C2D_DrawRectSolid(0, 152, 0, 320, 1, C2D_Color32(0x30, 0x34, 0x44, 0xFF));
    if (player_on_foot(p)) {
        font_draw("PAD  LOOK       A  WALK", 8, 160, 1.5f, dim);
        font_draw("X  JUMP", 8, 176, 1.5f, dim);
        if (c->gyro) font_draw("MOVE THE 3DS TO LOOK AROUND", 8, 192, 1.5f, dim);
        else if (c->first_person) font_draw("(GYRO BUTTON: MOTION LOOK)", 8, 192, 1.5f, dim);
        else font_draw("(VIEW BUTTON: FIRST PERSON)", 8, 192, 1.5f, dim);
    } else {
        font_draw("PAD  LOOK       L/R ROLL", 8, 160, 1.5f, dim);
        font_draw("X  UP     B  DOWN", 8, 176, 1.5f, dim);
        font_draw("A  BOOST  Y  BRAKE", 8, 192, 1.5f, dim);
    }
    font_draw("SELECT RESPAWN   START QUIT", 8, 216, 1.5f, dim);
}
