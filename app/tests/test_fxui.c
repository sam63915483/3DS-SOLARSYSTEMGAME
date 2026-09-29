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

    /* Keyboard preset button and page. */
    fxui_key(&u, BTN_UP, 0, &ch);
    CHECK(u.onPresetButton && !u.presetOpen, "up from the tab bar focuses the preset button");
    fxui_key(&u, BTN_LEFT, 0, &ch);
    CHECK(u.tab == FX_FLANGER, "left/right do nothing on the button");
    fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.presetOpen && u.presetRow == INST_SAMPLE, "A opens the page on the current preset");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch);
    r = fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.instrument == INST_BELLS && r == FXUI_CHANGED_INST, "A selects GLASS BELLS");
    for (int i = 0; i < 10; i++) fxui_key(&u, BTN_DOWN, 0, &ch);
    CHECK(u.presetRow == INST_COUNT - 1, "highlight clamps at the last row");
    fxui_key(&u, BTN_B, 0, &ch);
    CHECK(!u.presetOpen && u.onPresetButton && u.instrument == INST_BELLS, "B closes, choice kept");
    fxui_key(&u, BTN_DOWN, 0, &ch);
    CHECK(!u.onPresetButton, "down returns to the tab bar");
    MT_END();
}
