#include "fxui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SLIDER_STEP 0.025f

void fxui_init(FxUi *u) {
    memset(u, 0, sizeof(*u));
    for (int i = 0; i < FX_COUNT; i++) fx_config_default(i, &u->cfg[i]);
    u->row = 1; u->topCol = 1; u->presetRow = 1; u->modesRow = 1;
    u->instrument = INST_SAMPLE;
    u->userMode = MODE_PRESETS[0].settings;
    u->loopOverride = -1;
}

const SampleMode *fxui_mode(FxUi *u) {
    u->effective = u->modePreset == 0 ? u->userMode : MODE_PRESETS[u->modePreset].settings;
    if (u->loopOverride >= 0) u->effective.loop = u->loopOverride;
    return &u->effective;
}

void fxui_toggle_loop(FxUi *u) {
    int next = !fxui_mode(u)->loop;
    if (u->modePreset == 0) { u->userMode.loop = next; u->loopOverride = -1; }
    else u->loopOverride = next;
}

/* ---- Row models ---- */

static UiRow *add_row(PageView *pv, int type, const char *label) {
    UiRow *r = &pv->rows[pv->n++];
    memset(r, 0, sizeof(*r));
    r->type = type; r->editable = 1;
    snprintf(r->label, sizeof r->label, "%s", label);
    return r;
}

static void effect_rows(const FxUi *u, PageView *pv) {
    int fx = u->tab; const FxConfig *c = &u->cfg[fx]; const FxDef *d = &FX_DEFS[fx];
    pv->n = 0;
    add_row(pv, ROW_HEADER, "EFFECT");
    add_row(pv, ROW_TOGGLE, "POWER")->on = c->enabled;
    add_row(pv, ROW_HEADER, "PRESETS");
    for (int i = 0; i < FX_PRESETS; i++) { UiRow *r = add_row(pv, ROW_PRESET, d->presets[i].name); r->selected = i == c->preset; r->index = i; }
    add_row(pv, ROW_HEADER, "SETTINGS");
    for (int i = 0; i < FX_PARAMS; i++) {
        UiRow *r = add_row(pv, ROW_SLIDER, d->params[i].label);
        r->t = fx_to_norm(&d->params[i], c->values[i]); r->index = i;
        fx_format_value(&d->params[i], c->values[i], r->value);
    }
}

static void modes_rows(FxUi *u, PageView *pv) {
    int locked = MODE_PRESETS[u->modePreset].locked;
    const SampleMode *mode = fxui_mode(u);
    pv->n = 0;
    add_row(pv, ROW_HEADER, "PRESET");
    for (int i = 0; i < MODE_PRESET_COUNT; i++) {
        char label[48]; snprintf(label, sizeof label, "%s%s", MODE_PRESETS[i].name, MODE_PRESETS[i].locked ? "" : " (editable)");
        UiRow *r = add_row(pv, ROW_PRESET, label); r->selected = i == u->modePreset; r->index = i; r->desc = MODE_PRESETS[i].desc;
    }
    add_row(pv, ROW_HEADER, locked ? "SETTINGS  (fixed: select USER to edit)" : "SETTINGS");
    for (int f = 0; f < MODE_FIELD_COUNT; f++) {
        const ModeField *fd = &MODE_FIELDS[f];
        float value = mode_get(mode, f);
        if (fd->nChoices) {
            int idx = 0; for (int k = 0; k < fd->nChoices; k++) if (fabsf(fd->choices[k] - value) < 1e-3f) idx = k;
            UiRow *r = add_row(pv, ROW_CHOICE, fd->label);
            r->canLeft = idx > 0; r->canRight = idx < fd->nChoices - 1;
            r->editable = !locked; r->index = f; mode_format(f, value, r->value);
        } else {
            UiRow *r = add_row(pv, ROW_SLIDER, fd->label);
            r->t = (value - fd->min) / (fd->max - fd->min); r->editable = !locked; r->index = f;
            mode_format(f, value, r->value);
        }
    }
}

static void preset_rows(const FxUi *u, PageView *pv) {
    pv->n = 0;
    add_row(pv, ROW_HEADER, "KEYBOARD SOUND");
    for (int i = 0; i < INST_COUNT; i++) { UiRow *r = add_row(pv, ROW_PRESET, INST_NAMES[i]); r->selected = i == u->instrument; r->index = i; r->desc = INST_DESCS[i]; }
}

static int move_focus(const PageView *pv, int row, int dir) {
    int i = row + dir;
    while (i >= 0 && i < pv->n && pv->rows[i].type == ROW_HEADER) i += dir;
    return (i >= 0 && i < pv->n) ? i : row;
}

static int ensure_visible(const PageView *pv, int focus, int scroll) {
    int top = focus;
    if (top > 0 && pv->rows[top - 1].type == ROW_HEADER) top--;
    if (top < scroll) return top;
    if (focus >= scroll + VISIBLE_ROWS) return focus - VISIBLE_ROWS + 1;
    int maxScroll = pv->n - VISIBLE_ROWS; if (maxScroll < 0) maxScroll = 0;
    return scroll > maxScroll ? maxScroll : scroll;
}

int fxui_page(const FxUi *cu, PageView *pv) {
    FxUi *u = (FxUi *)cu;
    if (u->presetOpen) {
        preset_rows(u, pv);
        pv->title = "KEYBOARD PRESET"; snprintf(pv->right, sizeof pv->right, "A select   B back");
        pv->focus = u->presetRow; pv->scroll = u->presetScroll;
        snprintf(pv->status, sizeof pv->status, "%s", pv->rows[pv->focus].desc ? pv->rows[pv->focus].desc : "");
        return 1;
    }
    if (u->modesOpen) {
        modes_rows(u, pv);
        const ModePreset *p = &MODE_PRESETS[u->modePreset];
        pv->title = "MODES"; snprintf(pv->right, sizeof pv->right, "%s   A select   B back", p->name);
        pv->focus = u->modesRow; pv->scroll = u->modesScroll;
        const UiRow *r = &pv->rows[pv->focus];
        if (r->desc) snprintf(pv->status, sizeof pv->status, "%s", r->desc);
        else if (p->locked) snprintf(pv->status, sizeof pv->status, "%s is fixed. Select USER to edit the settings", p->name);
        else snprintf(pv->status, sizeof pv->status, "USER: left/right changes any setting");
        return 1;
    }
    if (u->open) {
        effect_rows(u, pv);
        int fx = u->tab; const FxConfig *c = &u->cfg[fx];
        pv->title = FX_DEFS[fx].name;
        snprintf(pv->right, sizeof pv->right, "%s%s   %s", FX_DEFS[fx].presets[c->preset].name, fx_config_is_tweaked(fx, c) ? " (tweaked)" : "", c->enabled ? "ON" : "OFF");
        pv->focus = u->row; pv->scroll = u->scroll;
        snprintf(pv->status, sizeof pv->status, "A toggles power or picks a preset   left/right adjusts sliders   B back");
        return 1;
    }
    return 0;
}

/* ---- Input ---- */

int fxui_key(FxUi *u, int key, int repeat, int *changedFx) {
    static PageView pv;
    int fx = u->tab;
    FxConfig *c = &u->cfg[fx];
    *changedFx = fx;

    if (u->presetOpen) {
        preset_rows(u, &pv);
        int result = 0;
        if (key == BTN_UP || key == BTN_DOWN) u->presetRow = move_focus(&pv, u->presetRow, key == BTN_UP ? -1 : 1);
        else if (key == BTN_A && !repeat) { u->instrument = pv.rows[u->presetRow].index; result = FXUI_CHANGED_INST; }
        else if (key == BTN_B && !repeat) u->presetOpen = 0;
        u->presetScroll = ensure_visible(&pv, u->presetRow, u->presetScroll);
        return result;
    }

    if (u->modesOpen) {
        modes_rows(u, &pv);
        const UiRow *r = &pv.rows[u->modesRow];
        int result = 0;
        if (key == BTN_UP || key == BTN_DOWN) u->modesRow = move_focus(&pv, u->modesRow, key == BTN_UP ? -1 : 1);
        else if (key == BTN_B) { if (!repeat) u->modesOpen = 0; }
        else if (r->type == ROW_PRESET) {
            if (key == BTN_A && !repeat) { u->modePreset = r->index; u->loopOverride = -1; result = FXUI_CHANGED_MODE; }
        } else if (r->editable && (key == BTN_LEFT || key == BTN_RIGHT)) {
            int f = r->index;
            if (!(MODE_FIELDS[f].nChoices && repeat)) {
                mode_set(&u->userMode, f, mode_step(f, mode_get(&u->userMode, f), key == BTN_LEFT ? -1 : 1));
                if (f == MF_LOOP) u->loopOverride = -1;
                result = FXUI_CHANGED_MODE;
            }
        }
        u->modesScroll = ensure_visible(&pv, u->modesRow, u->modesScroll);
        return result;
    }

    if (!u->open) {
        if (repeat) return 0;
        if (u->onTopRow) {
            if (key == BTN_DOWN) u->onTopRow = 0;
            else if (key == BTN_LEFT) u->topCol = 0;
            else if (key == BTN_RIGHT) u->topCol = 1;
            else if (key == BTN_A) {
                if (u->topCol == 1) { u->presetOpen = 1; u->presetRow = 1 + u->instrument; preset_rows(u, &pv); u->presetScroll = ensure_visible(&pv, u->presetRow, 0); }
                else { u->modesOpen = 1; u->modesRow = 1 + u->modePreset; u->modesScroll = 0; }
            }
            return 0;
        }
        if (key == BTN_UP) u->onTopRow = 1;
        else if (key == BTN_LEFT) u->tab = (u->tab + FX_COUNT - 1) % FX_COUNT;
        else if (key == BTN_RIGHT) u->tab = (u->tab + 1) % FX_COUNT;
        else if (key == BTN_A) { u->open = 1; u->row = 1; u->scroll = 0; }
        return 0;
    }

    effect_rows(u, &pv);
    const UiRow *r = &pv.rows[u->row];
    int result = 0;
    if (key == BTN_UP || key == BTN_DOWN) u->row = move_focus(&pv, u->row, key == BTN_UP ? -1 : 1);
    else if (key == BTN_B) { if (!repeat) u->open = 0; }
    else if (r->type == ROW_TOGGLE) {
        if (!repeat && (key == BTN_A || key == BTN_LEFT || key == BTN_RIGHT)) { c->enabled = !c->enabled; result = FXUI_CHANGED_FX; }
    } else if (r->type == ROW_PRESET) {
        if (key == BTN_A && !repeat) { fx_config_load_preset(fx, c, r->index); result = FXUI_CHANGED_FX; }
    } else if (r->type == ROW_SLIDER && (key == BTN_LEFT || key == BTN_RIGHT)) {
        int p = r->index;
        const FxParam *def = &FX_DEFS[fx].params[p];
        c->values[p] = fx_from_norm(def, fx_to_norm(def, c->values[p]) + (key == BTN_LEFT ? -1 : 1) * SLIDER_STEP);
        result = FXUI_CHANGED_FX;
    }
    u->scroll = ensure_visible(&pv, u->row, u->scroll);
    return result;
}
