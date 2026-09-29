#include "minitest.h"
#include "fxui.h"

int main(void) {
    FxUi u; int ch;
    fxui_init(&u);
    fxui_key(&u, BTN_LEFT, 0, &ch);
    CHECK(u.tab == FX_COUNT - 1, "left from first tab wraps to last");
    for (int i = 0; i < FX_FLANGER + 1; i++) fxui_key(&u, BTN_RIGHT, 0, &ch);
    CHECK(u.tab == FX_FLANGER && !u.open, "right moves to FLANGER");
    fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.open && u.row == 0, "A opens the panel at the power row");
    int r = fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.cfg[FX_FLANGER].enabled && r == FXUI_CHANGED_FX && ch == FX_FLANGER, "A on power turns it on and reports the change");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_RIGHT, 0, &ch); fxui_key(&u, BTN_RIGHT, 0, &ch);
    CHECK(u.row == 1 && u.col == 2 && u.cfg[FX_FLANGER].preset == 0, "moving over presets does not select");
    fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.cfg[FX_FLANGER].preset == 2 && u.cfg[FX_FLANGER].values[2] == 0.85f, "A selects METALLIC and loads its values");
    fxui_key(&u, BTN_DOWN, 0, &ch);
    float before = u.cfg[FX_FLANGER].values[0];
    r = fxui_key(&u, BTN_RIGHT, 1, &ch);
    CHECK(u.cfg[FX_FLANGER].values[0] > before && r == FXUI_CHANGED_FX, "slider nudges on repeat and reports");
    for (int i = 0; i < 100; i++) fxui_key(&u, BTN_RIGHT, 1, &ch);
    CHECK_NEAR(u.cfg[FX_FLANGER].values[0], 5.0f, 1e-4, "slider clamps at max");
    CHECK(fx_config_is_tweaked(FX_FLANGER, &u.cfg[FX_FLANGER]), "tweaked flag set");
    for (int i = 0; i < 20; i++) fxui_key(&u, BTN_DOWN, 0, &ch);
    CHECK(u.row == 5, "down clamps at the last slider");
    fxui_key(&u, BTN_B, 0, &ch);
    CHECK(!u.open && u.cfg[FX_FLANGER].enabled, "B closes and the effect stays on");

    /* Top row: MODES (left) and PRESET (right). */
    fxui_key(&u, BTN_UP, 0, &ch);
    CHECK(u.onTopRow && u.topCol == 1, "up from the tab bar focuses the top row on PRESET");
    fxui_key(&u, BTN_LEFT, 0, &ch); fxui_key(&u, BTN_RIGHT, 0, &ch);
    CHECK(u.topCol == 1 && u.tab == FX_FLANGER, "left/right switch between MODES and PRESET, tabs untouched");
    fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.presetOpen && u.presetRow == INST_SAMPLE, "A opens the keyboard preset page on the current preset");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch);
    r = fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.instrument == INST_BELLS && r == FXUI_CHANGED_INST, "A selects GLASS BELLS");
    for (int i = 0; i < 10; i++) fxui_key(&u, BTN_DOWN, 0, &ch);
    CHECK(u.presetRow == INST_COUNT - 1, "highlight clamps at the last row");
    fxui_key(&u, BTN_B, 0, &ch);
    CHECK(!u.presetOpen && u.onTopRow && u.instrument == INST_BELLS, "B closes, choice kept");

    /* Modes page. */
    fxui_key(&u, BTN_LEFT, 0, &ch); fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.modesOpen && u.modesRow == 0 && u.modesCol == 0, "MODES opens on the preset row");
    fxui_key(&u, BTN_RIGHT, 0, &ch);
    r = fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.modePreset == 1 && fxui_mode(&u)->rate == 9380 && r == FXUI_CHANGED_MODE, "A on SK-1 selects it and reports a mode change");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch);
    r = fxui_key(&u, BTN_RIGHT, 0, &ch);
    CHECK(fxui_mode(&u)->rate == 9380 && r == 0, "locked preset: settings cannot change");
    fxui_key(&u, BTN_UP, 0, &ch); fxui_key(&u, BTN_UP, 0, &ch); fxui_key(&u, BTN_LEFT, 0, &ch); fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.modePreset == 0, "back to USER");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch);
    r = fxui_key(&u, BTN_LEFT, 0, &ch);
    CHECK(fxui_mode(&u)->rate == 26040 && r == FXUI_CHANGED_MODE, "USER: left on RATE steps down and reports");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_RIGHT, 0, &ch);
    CHECK(fxui_mode(&u)->loop == 0, "LOOP toggles to one-shot");
    for (int i = 0; i < 20; i++) fxui_key(&u, BTN_DOWN, 0, &ch);
    CHECK(u.modesRow == MODE_FIELD_COUNT, "down clamps at the last field");
    for (int i = 0; i < 100; i++) fxui_key(&u, BTN_RIGHT, 1, &ch);
    CHECK_NEAR(fxui_mode(&u)->release, 3000, 1e-3, "RELEASE slider repeats and clamps at max");
    fxui_key(&u, BTN_B, 0, &ch);
    CHECK(!u.modesOpen && u.onTopRow, "B closes the modes page, focus stays on the top row");
    fxui_key(&u, BTN_DOWN, 0, &ch);
    CHECK(!u.onTopRow, "down returns to the tab bar");
    MT_END();
}
