#include "keyboard.h"
#include <stdio.h>

static const int ZOOM[ZOOM_LEVELS] = {5, 7, 10, 14, 20, 28, 36};
static const char *NAMES[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

static int iround(double v) { return (int)(v < 0 ? v - 0.5 : v + 0.5); }

int rect_contains(Rect r, int px, int py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

void kb_init(Keyboard *kb) {
    int white = 0;
    for (int m = LOW_MIDI; m <= HIGH_MIDI; m++) {
        int pc = m % 12, i = m - LOW_MIDI;
        int black = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
        kb->isBlack[i] = black;
        kb->whiteIndex[i] = black ? white - 1 : white;
        if (!black) white++;
    }
    kb->whiteCount = white;
    kb->zoomIndex = 3;   /* 14 white keys = two octaves */
    kb->scroll = 7;      /* C3 at the left */
}

int kb_visible_white(const Keyboard *kb) { return ZOOM[kb->zoomIndex]; }
int kb_max_scroll(const Keyboard *kb) { return kb->whiteCount - kb_visible_white(kb); }

void kb_set_scroll(Keyboard *kb, int s) {
    int m = kb_max_scroll(kb);
    kb->scroll = s < 0 ? 0 : (s > m ? m : s);
}

static void set_zoom(Keyboard *kb, int idx) {
    if (idx < 0) idx = 0;
    if (idx >= ZOOM_LEVELS) idx = ZOOM_LEVELS - 1;
    if (idx == kb->zoomIndex) return;
    double centre = kb->scroll + kb_visible_white(kb) / 2.0;
    kb->zoomIndex = idx;
    kb_set_scroll(kb, iround(centre - kb_visible_white(kb) / 2.0));
}
void kb_zoom_in(Keyboard *kb) { set_zoom(kb, kb->zoomIndex - 1); }
void kb_zoom_out(Keyboard *kb) { set_zoom(kb, kb->zoomIndex + 1); }

void kb_visible_range(const Keyboard *kb, int *lo, int *hi) {
    int first = kb->scroll, last = kb->scroll + kb_visible_white(kb) - 1;
    *lo = -1; *hi = -1;
    for (int i = 0; i < KEY_COUNT; i++) {
        if (kb->isBlack[i]) continue;
        if (kb->whiteIndex[i] == first) *lo = LOW_MIDI + i;
        if (kb->whiteIndex[i] == last) *hi = LOW_MIDI + i;
    }
}

int kb_layout(const Keyboard *kb, Rect area, KeyRect *out, int max) {
    int n = 0, vis = kb_visible_white(kb);
    double ww = (double)area.w / vis;
    for (int i = 0; i < KEY_COUNT && n < max; i++) {
        if (kb->isBlack[i]) continue;
        int col = kb->whiteIndex[i] - kb->scroll;
        if (col < 0 || col >= vis) continue;
        int x0 = iround(area.x + col * ww), x1 = iround(area.x + (col + 1) * ww);
        out[n].midi = LOW_MIDI + i; out[n].black = 0;
        out[n].r.x = x0; out[n].r.y = area.y; out[n].r.w = x1 - x0; out[n].r.h = area.h;
        n++;
    }
    int bw = iround(ww * 0.6); if (bw < 4) bw = 4;
    int bh = iround(area.h * 0.6);
    for (int i = 0; i < KEY_COUNT && n < max; i++) {
        if (!kb->isBlack[i]) continue;
        int boundary = kb->whiteIndex[i] + 1 - kb->scroll;  /* black keys sit on the boundary */
        if (boundary < 0 || boundary > vis) continue;
        double cx = area.x + boundary * ww;
        out[n].midi = LOW_MIDI + i; out[n].black = 1;
        out[n].r.x = iround(cx - bw / 2.0); out[n].r.y = area.y; out[n].r.w = bw; out[n].r.h = bh;
        n++;
    }
    return n;
}

int kb_hit(const Keyboard *kb, Rect area, int px, int py) {
    KeyRect rects[KEY_COUNT];
    int n = kb_layout(kb, area, rects, KEY_COUNT);
    for (int i = n - 1; i >= 0; i--)
        if (rect_contains(rects[i].r, px, py)) return rects[i].midi;
    return -1;
}

void kb_note_name(int midi, char out[8]) {
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    snprintf(out, 8, "%s%d", NAMES[midi % 12], midi / 12 - 1);
}
