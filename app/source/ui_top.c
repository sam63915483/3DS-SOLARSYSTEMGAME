/* Top screen: scope / recording / sample views, effects tab bar and panel, keyboard preset
 * button and page. Port of drawTop in screens.js and the drawing half of fxui.js. */
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include "ui_top.h"
#include "draw.h"
#include "platform_mic.h"

#define TAB_Y 212
#define TAB_H 24
static const Rect R_MODES = {8, 7, 100, 22};
static const Rect R_PRESET = {292, 7, 100, 22};

static u32 hsl(float h, float s, float l, float a) {   /* h 0..360, s/l 0..1 */
    float c = (1 - fabsf(2 * l - 1)) * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = l - c / 2;
    float r, g, b;
    if (h < 60) { r = c; g = x; b = 0; } else if (h < 120) { r = x; g = c; b = 0; } else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; } else if (h < 300) { r = x; g = 0; b = c; } else { r = c; g = 0; b = x; }
    return C2D_Color32((u8)((r + m) * 255), (u8)((g + m) * 255), (u8)((b + m) * 255), (u8)(a * 255));
}

void app_compute_sample_peaks(App *a) {
    const Sample *s = &a->mixer.sample;
    float perCol = (float)s->length / WAVE_COLUMNS;
    for (int c = 0; c < WAVE_COLUMNS; c++) {
        int start = (int)(c * perCol), end = (int)((c + 1) * perCol); if (end > s->length) end = s->length;
        float lo = 0, hi = 0;
        for (int i = start; i < end; i++) { if (s->data[i] < lo) lo = s->data[i]; if (s->data[i] > hi) hi = s->data[i]; }
        a->peakMin[c] = lo; a->peakMax[c] = hi;
    }
}

static void draw_peaks(Rect p, const float *mn, const float *mx, u32 color) {
    float mid = p.y + p.h / 2.0f, amp = p.h / 2.0f - 8;
    for (int c = 0; c < WAVE_COLUMNS; c++) {
        float top = mid - mx[c] * amp, bottom = mid - mn[c] * amp;
        float h = bottom - top; if (h < 1) h = 1;
        draw_rect(p.x + c, top, 1, h, color);
    }
}

static void draw_tab_bar(App *a) {
    FxUi *u = &a->fxui;
    float slot = 384.0f / FX_COUNT; int w = (int)slot - 4;
    draw_rect(0, TAB_Y - 4, TOP_W, TOP_H - TAB_Y + 4, RGB(0x0e, 0x0f, 0x12));
    for (int i = 0; i < FX_COUNT; i++) {
        int x = (int)(8 + i * slot + 0.5f);
        int focused = i == u->tab && !u->onTopRow, on = u->cfg[i].enabled;
        draw_box(x, TAB_Y, w, TAB_H, focused ? RGB(0x2c, 0x2d, 0x36) : RGB(0x15, 0x16, 0x1b), focused ? RGB(0xff, 0xb3, 0x47) : RGB(0x2c, 0x2d, 0x36), focused ? 2 : 1);
        if (on) draw_circle(x + 9, TAB_Y + TAB_H / 2.0f, 3, RGB(0x7e, 0xd9, 0x57));
        draw_text(FX_DEFS[i].name, x + w / 2.0f + (on ? 4 : 0), TAB_Y + TAB_H / 2.0f + 1, 12, on ? RGB(0x7e, 0xd9, 0x57) : (focused ? RGB(0xff, 0xff, 0xff) : RGB(0x9a, 0x9a, 0xa6)), ALIGN_CENTER);
    }
}

static void top_button(Rect r, const char *label, int focused) {
    draw_box(r.x, r.y, r.w, r.h, focused ? RGB(0x2c, 0x2d, 0x36) : RGB(0x15, 0x16, 0x1b), focused ? RGB(0xff, 0xb3, 0x47) : RGB(0x3a, 0x3a, 0x44), focused ? 2 : 1);
    draw_text(label, r.x + r.w / 2.0f, r.y + r.h / 2.0f + 1, 12, focused ? RGB(0xff, 0xff, 0xff) : RGB(0xc8, 0xc8, 0xd2), ALIGN_CENTER);
}

/* The header row shared by every non-page view: MODES, the title, PRESET. */
static void draw_header(App *a) {
    FxUi *u = &a->fxui;
    int focusable = u->onTopRow && !u->open && !u->presetOpen && !u->modesOpen;
    top_button(R_MODES, "MODES", focusable && u->topCol == 0);
    top_button(R_PRESET, "♪ PRESET", focusable && u->topCol == 1);
    draw_text("SK-1 CLONE", 200, 18, 14, RGB(0xff, 0x6b, 0x5b), ALIGN_CENTER);
}

/* Generic list page: title bar, six 30 px rows, scrollbar, status line. Sized for 400x240. */
#define ROW_H 30
#define ROWS_TOP 36

static void draw_row(const UiRow *r, int y, int focused) {
    int dim = !r->editable;
    float mid = y + ROW_H / 2.0f + 1;
    if (focused) draw_box(12, y + 2, 372, ROW_H - 4, RGB(0x2c, 0x2d, 0x36), RGB(0xff, 0xb3, 0x47), 2);
    if (r->type == ROW_HEADER) {
        draw_text(r->label, 16, mid + 4, 11, RGB(0x6a, 0x6a, 0x78), ALIGN_LEFT);
        draw_rect(12, y + ROW_H - 2, 372, 1, RGB(0x2c, 0x2d, 0x36));
        return;
    }
    u32 labelColor = focused ? RGB(0xff, 0xff, 0xff) : (dim ? RGB(0x5a, 0x5a, 0x66) : RGB(0xc8, 0xc8, 0xd2));
    u32 valueColor = focused ? RGB(0xff, 0xb3, 0x47) : (dim ? RGB(0x6a, 0x6a, 0x78) : RGB(0xc8, 0xc8, 0xd2));
    char txt[64];
    switch (r->type) {
    case ROW_TOGGLE:
        draw_text(r->label, 26, mid, 15, labelColor, ALIGN_LEFT);
        draw_text(r->on ? "● ON" : "○ OFF", 372, mid, 15, r->on ? RGB(0x7e, 0xd9, 0x57) : RGB(0x9a, 0x9a, 0xa6), ALIGN_RIGHT);
        break;
    case ROW_PRESET:
        snprintf(txt, sizeof txt, "%s%s", r->selected ? "● " : "   ", r->label);
        draw_text(txt, 26, mid, 15, r->selected ? RGB(0xff, 0xb3, 0x47) : labelColor, ALIGN_LEFT);
        break;
    case ROW_CHOICE:
        draw_text(r->label, 26, mid, 14, labelColor, ALIGN_LEFT);
        snprintf(txt, sizeof txt, "%s  %s  %s", r->editable && r->canLeft ? "<" : " ", r->value, r->editable && r->canRight ? ">" : " ");
        draw_text(txt, 250, mid, 15, valueColor, ALIGN_CENTER);
        break;
    case ROW_SLIDER: {
        draw_text(r->label, 26, mid, 14, labelColor, ALIGN_LEFT);
        int tx = 150, tw = 150;
        draw_box(tx, mid - 6, tw, 12, RGB(0x1a, 0x1a, 0x20), focused ? RGB(0xff, 0xb3, 0x47) : RGB(0x2c, 0x2d, 0x36), 1);
        int fillW = (int)(tw * r->t + 0.5f);
        if (fillW > 0) draw_rect(tx, mid - 6, fillW, 12, focused ? RGB(0xff, 0xb3, 0x47) : (dim ? RGB(0x3a, 0x3a, 0x44) : RGB(0x6c, 0x6c, 0x7a)));
        draw_circle(tx + fillW, mid, 7, focused ? RGB(0xff, 0xff, 0xff) : (dim ? RGB(0x6a, 0x6a, 0x78) : RGB(0xc8, 0xc8, 0xd2)));
        draw_text(r->value, 372, mid, 14, valueColor, ALIGN_RIGHT);
        break; }
    }
}

static void draw_page(const PageView *pv) {
    draw_rect(0, 0, TOP_W, TOP_H, RGB(0x0e, 0x0f, 0x12));
    draw_text(pv->title, 16, 17, 18, RGB(0xff, 0x6b, 0x5b), ALIGN_LEFT);
    draw_text(pv->right, 384, 18, 12, RGB(0x6a, 0x6a, 0x78), ALIGN_RIGHT);
    int end = pv->scroll + VISIBLE_ROWS; if (end > pv->n) end = pv->n;
    for (int i = pv->scroll; i < end; i++) draw_row(&pv->rows[i], ROWS_TOP + (i - pv->scroll) * ROW_H, i == pv->focus);
    if (pv->n > VISIBLE_ROWS) {
        int trackH = VISIBLE_ROWS * ROW_H;
        draw_rect(392, ROWS_TOP, 4, trackH, RGB(0x1a, 0x1a, 0x20));
        float thumbH = trackH * (float)VISIBLE_ROWS / pv->n; if (thumbH < 12) thumbH = 12;
        float thumbY = ROWS_TOP + (trackH - thumbH) * pv->scroll / (float)(pv->n - VISIBLE_ROWS);
        draw_rect(392, thumbY, 4, thumbH, RGB(0x6c, 0x6c, 0x7a));
    }
    draw_text(pv->status, 200, 228, 12, RGB(0x8a, 0x8a, 0x96), ALIGN_CENTER);
}

/* 16 Goertzel bins between 60 Hz and 6 kHz on the last mixer block: a cheap spectrum. */
static void spectrum(const float *x, int n, float *bins, int nb) {
    for (int b = 0; b < nb; b++) {
        float hz = 60.0f * powf(100.0f, (float)b / (nb - 1));
        float w = 2 * cosf(2 * 3.14159265f * hz / SR), s0 = 0, s1 = 0, s2 = 0;
        for (int i = 0; i < n; i++) { s0 = x[i] + w * s1 - s2; s2 = s1; s1 = s0; }
        bins[b] = sqrtf(s1 * s1 + s2 * s2 - w * s1 * s2) / (n / 2.0f);
    }
}

static void draw_scope(App *a) {
    Mixer *m = &a->mixer;
    const float CX = 200, CY = 128;
    draw_rect(0, 0, TOP_W, TOP_H, RGB(0x0e, 0x0f, 0x12));
    float rms = m->scopeRms; int midi = m->scopeMidi;
    float hue = midi < 0 ? 200 : (midi % 12) * 30.0f;
    float loud = rms * 3; if (loud > 1) loud = 1;
    int active = rms > 0.003f;

    int len = m->scopeLen > 0 ? m->scopeLen : BLOCK;
    if (active) {
        float bins[16]; spectrum(m->scope, len, bins, 16);
        float bw = TOP_W / 16.0f;
        for (int b = 0; b < 16; b++) {
            float h = bins[b] * 4 * 100; if (h > 100) h = 100; if (h < 1) continue;
            draw_rect(b * bw + 1, CY - h, bw - 2, h * 2, hsl(fmodf(hue + b * 12, 360), 0.8f, 0.55f, 0.35f));
        }
    }
    float baseR = 26 + loud * 46;
    u32 ring = hsl(fmodf(hue + 180, 360), 0.85f, 0.65f, active ? 0.9f : 0.25f);
    const int N = 90;
    float px = 0, py = 0;
    for (int i = 0; i <= N; i++) {
        float ang = (float)i / N * 2 * 3.14159265f - 3.14159265f / 2;
        float s = active ? m->scope[(i % N) * len / N] : 0;
        float r = baseR + s * 60;
        float x = CX + cosf(ang) * r, y = CY + sinf(ang) * r;
        if (i) draw_line(px, py, x, y, 1.5f, ring);
        px = x; py = y;
    }
    u32 trace = hsl(hue, 0.9f, active ? 0.65f : 0.35f, 1);
    if (active) {
        const int STEP = 4;
        for (int x = STEP; x < TOP_W; x += STEP) {
            float y0 = CY - m->scope[(x - STEP) * len / TOP_W] * 90, y1 = CY - m->scope[x * len / TOP_W] * 90;
            draw_line(x - STEP, y0, x, y1, 2, trace);
        }
    } else draw_line(0, CY, TOP_W, CY, 2, trace);

    /* note name under the title (the header row is drawn by the caller) */
    if (midi >= 0) { char txt[8]; kb_note_name(midi, txt); draw_text(txt, 200, 44, 18, hsl(hue, 0.9f, 0.7f, 1), ALIGN_CENTER); }
    else draw_text("–", 200, 44, 18, RGB(0x3a, 0x3a, 0x44), ALIGN_CENTER);
}

void ui_top_draw(App *a) {
    FxUi *u = &a->fxui;
    static PageView pv;   /* ~2 KB: keep off the stack */
    if (fxui_page(u, &pv)) { draw_page(&pv); return; }
    if (a->topMode == TOP_SCOPE) { draw_scope(a); draw_tab_bar(a); draw_header(a); }
    else {
        char txt[96];
        draw_rect(0, 0, TOP_W, TOP_H, RGB(0x0e, 0x0f, 0x12));
        Rect p = {16, 40, WAVE_COLUMNS, TAB_Y - 8 - 40};
        draw_box(p.x, p.y, p.w, p.h, RGB(0x15, 0x16, 0x1b), RGB(0x2c, 0x2d, 0x36), 1);
        draw_rect(p.x, p.y + p.h / 2.0f, p.w, 1, RGB(0x2c, 0x2d, 0x36));
        if (a->topMode == TOP_RECORDING) {
            draw_peaks(p, mic_live_min(), mic_live_max(), RGB(0xff, 0x6b, 0x5b));
            float secs = mic_seconds();
            float maxSecs = mic_max_seconds();
            draw_rect(p.x, p.y + p.h - 4, p.w * secs / maxSecs, 4, RGB(0xc0, 0x39, 0x2b));
            if (mic_is_armed()) {
                snprintf(txt, sizeof txt, "● ARMED  make a sound to start the %.1f s window", maxSecs);
                draw_text(txt, p.x + 8, p.y + 12, 12, RGB(0xff, 0xb3, 0x47), ALIGN_LEFT);
            } else {
                snprintf(txt, sizeof txt, "● REC  %.1f s / %.1f s", secs, maxSecs);
                draw_text(txt, p.x + 8, p.y + 12, 12, RGB(0xff, 0x6b, 0x5b), ALIGN_LEFT);
            }
            float level = mic_level() * 4; if (level > 1) level = 1;
            draw_rect(p.x + p.w - 88, p.y + 8, 80, 8, RGB(0x2c, 0x2d, 0x36));
            draw_rect(p.x + p.w - 88, p.y + 8, 80 * level, 8, level > 0.9f ? RGB(0xff, 0x3b, 0x3b) : RGB(0x7e, 0xd9, 0x57));
        } else {
            draw_peaks(p, a->peakMin, a->peakMax, RGB(0xff, 0xb3, 0x47));
            const Sample *s = &a->mixer.sample;
            const char *label = s->isBuiltIn ? "BUILT-IN TONE" : "YOUR SAMPLE";
            float secs = (float)s->length / s->rate;
            if (s->trimmedMs && !s->isBuiltIn) snprintf(txt, sizeof txt, "%s  %.2f s   %.1f kHz %d-bit %s   (trimmed %d ms)", label, secs, s->rate / 1000.0f, a->mixer.mode.bits, s->loop ? "loop" : "one-shot", s->trimmedMs);
            else snprintf(txt, sizeof txt, "%s  %.2f s   %.1f kHz %d-bit %s", label, secs, s->rate / 1000.0f, a->mixer.mode.bits, s->loop ? "loop" : "one-shot");
            draw_text(txt, p.x + 8, p.y + 12, 11, RGB(0x8a, 0x8a, 0x96), ALIGN_LEFT);
            draw_text("PLAY A KEY FOR THE SCOPE", p.x + p.w - 8, p.y + p.h - 12, 11, RGB(0x6a, 0x6a, 0x78), ALIGN_RIGHT);
        }
        draw_tab_bar(a); draw_header(a);
    }
    if (!a->dspOk) draw_text("NO DSP FIRMWARE: run the DSP1 homebrew once, then restart", 16, TAB_Y - 12, 11, RGB(0xff, 0x3b, 0x3b), ALIGN_LEFT);
    else if (!a->micOk || mic_last_error()) {
        char err[48]; snprintf(err, sizeof err, "MIC %s: 0x%08X", a->micOk ? "START FAILED" : "UNAVAILABLE", mic_last_error());
        draw_text(err, 16, TAB_Y - 12, 11, RGB(0xff, 0x3b, 0x3b), ALIGN_LEFT);
    }
}
