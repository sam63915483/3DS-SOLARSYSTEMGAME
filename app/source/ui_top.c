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
        draw_text(FX_DEFS[i].name, x + w / 2.0f + (on ? 4 : 0), TAB_Y + TAB_H / 2.0f + 1, 11, on ? RGB(0x7e, 0xd9, 0x57) : (focused ? RGB(0xff, 0xff, 0xff) : RGB(0x9a, 0x9a, 0xa6)), ALIGN_CENTER);
    }
}

static void top_button(Rect r, const char *label, int focused) {
    draw_box(r.x, r.y, r.w, r.h, focused ? RGB(0x2c, 0x2d, 0x36) : RGB(0x15, 0x16, 0x1b), focused ? RGB(0xff, 0xb3, 0x47) : RGB(0x3a, 0x3a, 0x44), focused ? 2 : 1);
    draw_text(label, r.x + r.w / 2.0f, r.y + r.h / 2.0f + 1, 11, focused ? RGB(0xff, 0xff, 0xff) : RGB(0xc8, 0xc8, 0xd2), ALIGN_CENTER);
}

/* The header row shared by every non-page view: MODES, the title, PRESET. */
static void draw_header(App *a) {
    FxUi *u = &a->fxui;
    int focusable = u->onTopRow && !u->open && !u->presetOpen && !u->modesOpen;
    top_button(R_MODES, "MODES", focusable && u->topCol == 0);
    top_button(R_PRESET, "♪ PRESET", focusable && u->topCol == 1);
    draw_text("SK-1 CLONE", 200, 18, 14, RGB(0xff, 0x6b, 0x5b), ALIGN_CENTER);
}

static void draw_modes_page(App *a) {
    FxUi *u = &a->fxui;
    const ModePreset *preset = &MODE_PRESETS[u->modePreset];
    const SampleMode *mode = fxui_mode(u);
    char txt[48];
    draw_rect(0, 0, TOP_W, TOP_H, RGB(0x0e, 0x0f, 0x12));
    draw_text("MODES", 16, 20, 16, RGB(0xff, 0x6b, 0x5b), ALIGN_LEFT);
    if (preset->locked) snprintf(txt, sizeof txt, "%s is fixed. Select USER to edit settings", preset->name);
    else snprintf(txt, sizeof txt, "USER: edit any setting");
    draw_text(txt, 384, 21, 11, RGB(0x6a, 0x6a, 0x78), ALIGN_RIGHT);

    float slot = 368.0f / MODE_PRESET_COUNT;
    for (int i = 0; i < MODE_PRESET_COUNT; i++) {
        int x = (int)(16 + i * slot + 0.5f), w = (int)slot - 4;
        int selected = i == u->modePreset, focused = u->modesRow == 0 && i == u->modesCol;
        draw_box(x, 32, w, 24, selected ? RGB(0x5a, 0x3a, 0x14) : RGB(0x1a, 0x1a, 0x20), focused ? RGB(0xff, 0xb3, 0x47) : (selected ? RGB(0xa8, 0x6a, 0x2a) : RGB(0x2c, 0x2d, 0x36)), focused ? 2 : 1);
        snprintf(txt, sizeof txt, "%s%s", selected ? "● " : "", MODE_PRESETS[i].name);
        draw_text(txt, x + w / 2.0f, 45, 11, selected ? RGB(0xff, 0xb3, 0x47) : (focused ? RGB(0xff, 0xff, 0xff) : RGB(0x9a, 0x9a, 0xa6)), ALIGN_CENTER);
    }
    draw_text(MODE_PRESETS[u->modesRow == 0 ? u->modesCol : u->modePreset].desc, 16, 66, 10, RGB(0x6a, 0x6a, 0x78), ALIGN_LEFT);

    int editable = !preset->locked;
    for (int f = 0; f < MODE_FIELD_COUNT; f++) {
        int y = 84 + f * 18, focused = u->modesRow == 1 + f;
        const ModeField *fd = &MODE_FIELDS[f];
        float value = mode_get(mode, f);
        mode_format(f, value, txt);
        draw_text(fd->label, 16, y, 11, focused ? RGB(0xff, 0xff, 0xff) : (editable ? RGB(0x9a, 0x9a, 0xa6) : RGB(0x5a, 0x5a, 0x66)), ALIGN_LEFT);
        u32 valueColor = focused ? RGB(0xff, 0xb3, 0x47) : (editable ? RGB(0xc8, 0xc8, 0xd2) : RGB(0x6a, 0x6a, 0x78));
        if (fd->nChoices) {
            int idx = 0;
            for (int k = 0; k < fd->nChoices; k++) if (fabsf(fd->choices[k] - value) < 1e-3f) idx = k;
            char line[64];
            snprintf(line, sizeof line, "%s %s %s", idx > 0 && editable ? "<" : " ", txt, idx < fd->nChoices - 1 && editable ? ">" : " ");
            draw_text(line, 208, y, 11, valueColor, ALIGN_CENTER);
        } else {
            float t = (value - fd->min) / (fd->max - fd->min);
            draw_box(96, y - 5, 224, 10, RGB(0x1a, 0x1a, 0x20), focused ? RGB(0xff, 0xb3, 0x47) : RGB(0x2c, 0x2d, 0x36), 1);
            int fillW = (int)(224 * t + 0.5f);
            if (fillW > 0) draw_rect(96, y - 5, fillW, 10, focused ? RGB(0xff, 0xb3, 0x47) : (editable ? RGB(0x6c, 0x6c, 0x7a) : RGB(0x3a, 0x3a, 0x44)));
            draw_circle(96 + fillW, y, 5, focused ? RGB(0xff, 0xff, 0xff) : (editable ? RGB(0xc8, 0xc8, 0xd2) : RGB(0x6a, 0x6a, 0x78)));
            draw_text(txt, 384, y, 11, valueColor, ALIGN_RIGHT);
        }
    }
    draw_text("D-PAD up/down move   left/right change   A select preset   B close", 200, 232, 10, RGB(0x4a, 0x4a, 0x56), ALIGN_CENTER);
}

static void draw_preset_page(App *a) {
    FxUi *u = &a->fxui;
    draw_rect(0, 0, TOP_W, TOP_H, RGB(0x0e, 0x0f, 0x12));
    draw_text("KEYBOARD PRESET", 16, 20, 16, RGB(0xff, 0x6b, 0x5b), ALIGN_LEFT);
    for (int i = 0; i < INST_COUNT; i++) {
        int y = 36 + i * 30, selected = i == u->instrument, focused = i == u->presetRow;
        draw_box(16, y, 368, 26, selected ? RGB(0x5a, 0x3a, 0x14) : RGB(0x1a, 0x1a, 0x20), focused ? RGB(0xff, 0xb3, 0x47) : (selected ? RGB(0xa8, 0x6a, 0x2a) : RGB(0x2c, 0x2d, 0x36)), focused ? 2 : 1);
        char txt[40]; snprintf(txt, sizeof txt, "%s%s", selected ? "● " : "", INST_NAMES[i]);
        draw_text(txt, 26, y + 14, 11, selected ? RGB(0xff, 0xb3, 0x47) : (focused ? RGB(0xff, 0xff, 0xff) : RGB(0xc8, 0xc8, 0xd2)), ALIGN_LEFT);
        draw_text(INST_DESCS[i], 374, y + 14, 10, focused ? RGB(0xc8, 0xc8, 0xd2) : RGB(0x6a, 0x6a, 0x78), ALIGN_RIGHT);
    }
    draw_text("D-PAD move   A select   B close", 200, 229, 10, RGB(0x4a, 0x4a, 0x56), ALIGN_CENTER);
}

static void draw_fx_panel(App *a) {
    FxUi *u = &a->fxui;
    int fx = u->tab; FxConfig *c = &u->cfg[fx]; const FxDef *d = &FX_DEFS[fx];
    char txt[48];
    draw_rect(0, 0, TOP_W, TOP_H, RGB(0x0e, 0x0f, 0x12));
    draw_text(d->name, 16, 20, 16, RGB(0xff, 0x6b, 0x5b), ALIGN_LEFT);
    snprintf(txt, sizeof txt, "%s%s", d->presets[c->preset].name, fx_config_is_tweaked(fx, c) ? " (tweaked)" : "");
    draw_text(txt, 16 + draw_text_width(d->name, 16) + 30, 21, 11, RGB(0x6a, 0x6a, 0x78), ALIGN_LEFT);
    draw_text(c->enabled ? "EFFECT ON" : "EFFECT OFF", 384, 20, 12, c->enabled ? RGB(0x7e, 0xd9, 0x57) : RGB(0x6a, 0x6a, 0x78), ALIGN_RIGHT);

    int f0 = u->row == 0;
    draw_box(16, 34, 96, 26, c->enabled ? RGB(0x1f, 0x4d, 0x2a) : RGB(0x2a, 0x2a, 0x32), f0 ? RGB(0xff, 0xb3, 0x47) : RGB(0x3a, 0x3a, 0x44), f0 ? 2 : 1);
    draw_text(c->enabled ? "● ON" : "○ OFF", 64, 48, 12, c->enabled ? RGB(0x7e, 0xd9, 0x57) : RGB(0x9a, 0x9a, 0xa6), ALIGN_CENTER);

    float slot = 368.0f / FX_PRESETS;
    for (int i = 0; i < FX_PRESETS; i++) {
        int x = (int)(16 + i * slot + 0.5f), w = (int)slot - 4;
        int selected = i == c->preset, focused = u->row == 1 && i == u->col;
        draw_box(x, 68, w, 26, selected ? RGB(0x5a, 0x3a, 0x14) : RGB(0x1a, 0x1a, 0x20), focused ? RGB(0xff, 0xb3, 0x47) : (selected ? RGB(0xa8, 0x6a, 0x2a) : RGB(0x2c, 0x2d, 0x36)), focused ? 2 : 1);
        draw_text(d->presets[i].name, x + w / 2.0f, 82, 10, selected ? RGB(0xff, 0xb3, 0x47) : (focused ? RGB(0xff, 0xff, 0xff) : RGB(0x9a, 0x9a, 0xa6)), ALIGN_CENTER);
    }

    for (int i = 0; i < FX_PARAMS; i++) {
        int y = 110 + i * 28, focused = u->row == 2 + i;
        const FxParam *p = &d->params[i];
        float t = fx_to_norm(p, c->values[i]);
        draw_text(p->label, 16, y, 11, focused ? RGB(0xff, 0xff, 0xff) : RGB(0x9a, 0x9a, 0xa6), ALIGN_LEFT);
        draw_box(96, y - 5, 224, 10, RGB(0x1a, 0x1a, 0x20), focused ? RGB(0xff, 0xb3, 0x47) : RGB(0x2c, 0x2d, 0x36), 1);
        int fillW = (int)(224 * t + 0.5f);
        if (fillW > 0) draw_rect(96, y - 5, fillW, 10, focused ? RGB(0xff, 0xb3, 0x47) : RGB(0x6c, 0x6c, 0x7a));
        draw_circle(96 + fillW, y, 6, focused ? RGB(0xff, 0xff, 0xff) : RGB(0xc8, 0xc8, 0xd2));
        fx_format_value(p, c->values[i], txt);
        draw_text(txt, 384, y, 11, focused ? RGB(0xff, 0xb3, 0x47) : RGB(0xc8, 0xc8, 0xd2), ALIGN_RIGHT);
    }
    draw_text("D-PAD up/down move   left/right adjust   A select   B close", 200, 229, 10, RGB(0x4a, 0x4a, 0x56), ALIGN_CENTER);
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

    if (active) {
        float bins[16]; spectrum(m->scope, BLOCK, bins, 16);
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
        float s = active ? m->scope[(i % N) * BLOCK / N] : 0;
        float r = baseR + s * 60;
        float x = CX + cosf(ang) * r, y = CY + sinf(ang) * r;
        if (i) draw_line(px, py, x, y, 1.5f, ring);
        px = x; py = y;
    }
    u32 trace = hsl(hue, 0.9f, active ? 0.65f : 0.35f, 1);
    if (active) {
        const int STEP = 4;
        for (int x = STEP; x < TOP_W; x += STEP) {
            float y0 = CY - m->scope[(x - STEP) * BLOCK / TOP_W] * 90, y1 = CY - m->scope[x * BLOCK / TOP_W] * 90;
            draw_line(x - STEP, y0, x, y1, 2, trace);
        }
    } else draw_line(0, CY, TOP_W, CY, 2, trace);

    /* note name under the title (the header row is drawn by the caller) */
    if (midi >= 0) { char txt[8]; kb_note_name(midi, txt); draw_text(txt, 200, 44, 18, hsl(hue, 0.9f, 0.7f, 1), ALIGN_CENTER); }
    else draw_text("–", 200, 44, 18, RGB(0x3a, 0x3a, 0x44), ALIGN_CENTER);
}

void ui_top_draw(App *a) {
    FxUi *u = &a->fxui;
    if (u->presetOpen) { draw_preset_page(a); return; }
    if (u->modesOpen) { draw_modes_page(a); return; }
    if (u->open) { draw_fx_panel(a); return; }
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
            snprintf(txt, sizeof txt, "● REC  %.1f s / %g s", secs, maxSecs);
            draw_text(txt, p.x + 8, p.y + 12, 12, RGB(0xff, 0x6b, 0x5b), ALIGN_LEFT);
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
