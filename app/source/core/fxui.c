#include "fxui.h"

#define SLIDER_STEP 0.025f

void fxui_init(FxUi *u) {
    u->tab = 0; u->open = 0; u->row = 0; u->col = 0;
    for (int i = 0; i < FX_COUNT; i++) fx_config_default(i, &u->cfg[i]);
    u->onTopRow = 0; u->topCol = 1;
    u->presetOpen = 0; u->presetRow = 0; u->instrument = INST_SAMPLE;
    u->modesOpen = 0; u->modesRow = 0; u->modesCol = 0; u->modePreset = 0;
    u->userMode = MODE_PRESETS[0].settings;
}

const SampleMode *fxui_mode(const FxUi *u) {
    return u->modePreset == 0 ? &u->userMode : &MODE_PRESETS[u->modePreset].settings;
}

static int modes_key(FxUi *u, int key, int repeat) {
    int locked = MODE_PRESETS[u->modePreset].locked;
    if (key == BTN_B) { if (!repeat) u->modesOpen = 0; return 0; }
    if (key == BTN_UP) { if (!repeat && u->modesRow > 0) u->modesRow--; return 0; }
    if (key == BTN_DOWN) { if (!repeat && u->modesRow < MODE_FIELD_COUNT) u->modesRow++; return 0; }
    if (u->modesRow == 0) {
        if (repeat) return 0;
        if (key == BTN_LEFT && u->modesCol > 0) u->modesCol--;
        else if (key == BTN_RIGHT && u->modesCol < MODE_PRESET_COUNT - 1) u->modesCol++;
        else if (key == BTN_A) { u->modePreset = u->modesCol; return FXUI_CHANGED_MODE; }
        return 0;
    }
    if (locked) return 0;   /* only USER is editable */
    int field = u->modesRow - 1;
    if (key == BTN_LEFT || key == BTN_RIGHT) {
        if (MODE_FIELDS[field].nChoices && repeat) return 0;
        mode_set(&u->userMode, field, mode_step(field, mode_get(&u->userMode, field), key == BTN_LEFT ? -1 : 1));
        return FXUI_CHANGED_MODE;
    }
    return 0;
}

int fxui_key(FxUi *u, int key, int repeat, int *changedFx) {
    int fx = u->tab;
    FxConfig *c = &u->cfg[fx];
    *changedFx = fx;

    if (u->presetOpen) {
        if (repeat) return 0;
        if (key == BTN_UP && u->presetRow > 0) u->presetRow--;
        else if (key == BTN_DOWN && u->presetRow < INST_COUNT - 1) u->presetRow++;
        else if (key == BTN_A) { u->instrument = u->presetRow; return FXUI_CHANGED_INST; }
        else if (key == BTN_B) u->presetOpen = 0;
        return 0;
    }
    if (u->modesOpen) return modes_key(u, key, repeat);

    if (!u->open) {
        if (repeat) return 0;
        if (u->onTopRow) {
            if (key == BTN_DOWN) u->onTopRow = 0;
            else if (key == BTN_LEFT) u->topCol = 0;
            else if (key == BTN_RIGHT) u->topCol = 1;
            else if (key == BTN_A) {
                if (u->topCol == 1) { u->presetOpen = 1; u->presetRow = u->instrument; }
                else { u->modesOpen = 1; u->modesRow = 0; u->modesCol = u->modePreset; }
            }
            return 0;
        }
        if (key == BTN_UP) u->onTopRow = 1;
        else if (key == BTN_LEFT) u->tab = (u->tab + FX_COUNT - 1) % FX_COUNT;
        else if (key == BTN_RIGHT) u->tab = (u->tab + 1) % FX_COUNT;
        else if (key == BTN_A) { u->open = 1; u->row = 0; u->col = c->preset; }
        return 0;
    }

    int lastRow = 1 + FX_PARAMS;
    switch (key) {
    case BTN_B: if (!repeat) u->open = 0; return 0;
    case BTN_UP: if (!repeat && u->row > 0) u->row--; return 0;
    case BTN_DOWN: if (!repeat && u->row < lastRow) u->row++; return 0;
    case BTN_A:
        if (repeat) return 0;
        if (u->row == 0) { c->enabled = !c->enabled; return FXUI_CHANGED_FX; }
        if (u->row == 1) { fx_config_load_preset(fx, c, u->col); return FXUI_CHANGED_FX; }
        return 0;
    case BTN_LEFT: case BTN_RIGHT: {
        int dir = key == BTN_LEFT ? -1 : 1;
        if (u->row == 0) { if (repeat) return 0; c->enabled = !c->enabled; return FXUI_CHANGED_FX; }
        if (u->row == 1) {
            if (!repeat) { u->col += dir; if (u->col < 0) u->col = 0; if (u->col >= FX_PRESETS) u->col = FX_PRESETS - 1; }
            return 0;
        }
        int p = u->row - 2;
        const FxParam *def = &FX_DEFS[fx].params[p];
        c->values[p] = fx_from_norm(def, fx_to_norm(def, c->values[p]) + dir * SLIDER_STEP);
        return FXUI_CHANGED_FX;
    }
    }
    return 0;
}
