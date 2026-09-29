/* Bottom (touch) screen. Port of drawBottom in screens.js and the pointer handling in app.js. */
#include <3ds.h>
#include "ui_bottom.h"
#include "draw.h"
#include "platform_mic.h"

static const Rect R_REC   = {8, 6, 84, 36};
static const Rect R_MINUS = {228, 6, 40, 36};
static const Rect R_PLUS  = {272, 6, 40, 36};
static const Rect R_KEYS  = {0, 48, 320, 156};
static const Rect R_TRACK = {8, 212, 304, 22};

static Rect thumb_rect(const Keyboard *kb) {
    int w = R_TRACK.w * kb_visible_white(kb) / kb->whiteCount; if (w < 16) w = 16;
    int ms = kb_max_scroll(kb);
    int x = R_TRACK.x + (ms ? (R_TRACK.w - w) * kb->scroll / ms : 0);
    Rect r = {x, R_TRACK.y, w, R_TRACK.h};
    return r;
}

static void slider_to(App *a, int px) {
    Rect th = thumb_rect(&a->kb);
    int usable = R_TRACK.w - th.w;
    float frac = usable > 0 ? (float)(px - a->grabOffset - R_TRACK.x) / usable : 0;
    kb_set_scroll(&a->kb, (int)(frac * kb_max_scroll(&a->kb) + 0.5f));
}

static void press_at(App *a, int px, int py) {
    int midi = kb_hit(&a->kb, R_KEYS, px, py);
    if (midi < 0) { if (a->heldMidi >= 0) { a->heldMidi = -1; mixer_note_off(&a->mixer); } return; }
    if (midi == a->heldMidi) return;
    a->heldMidi = midi;
    mixer_note_on(&a->mixer, midi);
    if (!mic_is_recording()) a->topMode = TOP_SCOPE;
}

static void toggle_record(App *a) {
    if (mic_is_recording()) { mic_stop(&a->mixer); mixer_set_muted(&a->mixer, 0); }
    else if (a->micOk) { mixer_set_muted(&a->mixer, 1); if (!mic_start()) mixer_set_muted(&a->mixer, 0); }
}

void ui_bottom_input(App *a) {
    u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp();
    touchPosition t; hidTouchRead(&t);
    if (down & KEY_TOUCH) {
        if (rect_contains(R_REC, t.px, t.py)) toggle_record(a);
        else if (rect_contains(R_MINUS, t.px, t.py)) kb_zoom_out(&a->kb);
        else if (rect_contains(R_PLUS, t.px, t.py)) kb_zoom_in(&a->kb);
        else if (rect_contains(R_TRACK, t.px, t.py)) {
            Rect th = thumb_rect(&a->kb);
            a->grabOffset = rect_contains(th, t.px, t.py) ? t.px - th.x : th.w / 2;
            a->drag = 2; slider_to(a, t.px);
        } else if (rect_contains(R_KEYS, t.px, t.py)) { a->drag = 1; press_at(a, t.px, t.py); }
    } else if (held & KEY_TOUCH) {
        if (a->drag == 1) press_at(a, t.px, t.py);
        else if (a->drag == 2) slider_to(a, t.px);
    }
    if (up & KEY_TOUCH) {
        if (a->drag == 1 && a->heldMidi >= 0) { a->heldMidi = -1; mixer_note_off(&a->mixer); }
        a->drag = 0;
    }
}

static void button(Rect r, u32 fill, const char *label, u32 textColor) {
    draw_box(r.x, r.y, r.w, r.h, fill, RGB(0x55, 0x55, 0x6a), 1);
    draw_text(label, r.x + r.w / 2.0f, r.y + r.h / 2.0f + 1, 14, textColor, ALIGN_CENTER);
}

void ui_bottom_draw(App *a) {
    char txt[16];
    draw_rect(0, 0, BOT_W, BOT_H, RGB(0x20, 0x20, 0x24));
    if (mic_is_recording()) button(R_REC, RGB(0xc0, 0x39, 0x2b), "STOP", RGB(0xff, 0xff, 0xff));
    else button(R_REC, RGB(0x3a, 0x3a, 0x44), "● REC", RGB(0xff, 0x6b, 0x5b));
    button(R_MINUS, RGB(0x3a, 0x3a, 0x44), "−", RGB(0xff, 0xff, 0xff));
    button(R_PLUS, RGB(0x3a, 0x3a, 0x44), "+", RGB(0xff, 0xff, 0xff));

    if (a->heldMidi >= 0) { kb_note_name(a->heldMidi, txt); draw_text(txt, 160, 25, 24, RGB(0xff, 0xb3, 0x47), ALIGN_CENTER); }
    else draw_text("–", 160, 25, 24, RGB(0x3a, 0x3a, 0x44), ALIGN_CENTER);
    int bend = (int)(a->mixer.bend + (a->mixer.bend < 0 ? -0.5f : 0.5f));
    if (bend != 0) { snprintf(txt, sizeof txt, "%+d", bend); draw_text(txt, 190, 25, 11, RGB(0x7e, 0xd9, 0x57), ALIGN_LEFT); }

    KeyRect rects[KEY_COUNT];
    int n = kb_layout(&a->kb, R_KEYS, rects, KEY_COUNT);
    for (int i = 0; i < n; i++) {
        KeyRect *k = &rects[i];
        int pressed = k->midi == a->heldMidi;
        int x = k->r.x, w = k->r.w;
        if (x < R_KEYS.x) { w -= R_KEYS.x - x; x = R_KEYS.x; }
        if (x + w > R_KEYS.x + R_KEYS.w) w = R_KEYS.x + R_KEYS.w - x;
        if (w <= 0) continue;
        if (k->black) draw_box(x, k->r.y, w, k->r.h, pressed ? RGB(0xff, 0x9f, 0x43) : RGB(0x1a, 0x1a, 0x1e), RGB(0, 0, 0), 1);
        else {
            draw_box(x, k->r.y, w, k->r.h, pressed ? RGB(0xff, 0xb3, 0x47) : RGB(0xf2, 0xf0, 0xea), RGB(0x3a, 0x3a, 0x3a), 1);
            if (k->midi % 12 == 0 && w >= 18) { kb_note_name(k->midi, txt); draw_text(txt, x + w / 2.0f, k->r.y + k->r.h - 10, 10, RGB(0x6a, 0x6a, 0x6a), ALIGN_CENTER); }
        }
    }

    draw_box(R_TRACK.x, R_TRACK.y, R_TRACK.w, R_TRACK.h, RGB(0x11, 0x11, 0x14), RGB(0x3a, 0x3a, 0x44), 1);
    Rect th = thumb_rect(&a->kb);
    draw_rect(th.x + 1, th.y + 2, th.w - 2, th.h - 4, a->drag == 2 ? RGB(0xff, 0x9f, 0x43) : RGB(0x6c, 0x6c, 0x7a));
    float cx = th.x + th.w / 2.0f;
    for (int dx = -4; dx <= 4; dx += 4) draw_rect(cx + dx, th.y + 7, 1, th.h - 14, RGB(0x2a, 0x2a, 0x30));
}
