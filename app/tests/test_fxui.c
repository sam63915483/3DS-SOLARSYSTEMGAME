#include "minitest.h"
#include "fxui.h"
#include <string.h>

int main(void) {
    FxUi u; int ch; PageView pv;
    fxui_init(&u);
    fxui_key(&u, BTN_LEFT, 0, &ch);
    CHECK(u.tab == FX_COUNT - 1, "left from first tab wraps to last");
    for (int i = 0; i < FX_FLANGER + 1; i++) fxui_key(&u, BTN_RIGHT, 0, &ch);
    CHECK(u.tab == FX_FLANGER && !u.open, "right moves to FLANGER");

    /* Effect panel as a scrolling list. */
    fxui_key(&u, BTN_A, 0, &ch);
    CHECK(fxui_page(&u, &pv) && u.open && u.row == 1 && pv.rows[1].type == ROW_TOGGLE, "A opens the panel focused on POWER");
    CHECK(pv.n == 13, "panel has 13 rows (3 headers, power, 5 presets, 4 sliders)");
    int r = fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.cfg[FX_FLANGER].enabled && r == FXUI_CHANGED_FX && ch == FX_FLANGER, "A on POWER turns it on and reports");
    fxui_key(&u, BTN_DOWN, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(u.row == 3 && pv.rows[3].type == ROW_PRESET, "down skips the header onto the first preset");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.cfg[FX_FLANGER].preset == 2 && u.cfg[FX_FLANGER].values[2] == 0.85f, "A on the third preset selects METALLIC");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(u.row == 9 && pv.rows[9].type == ROW_SLIDER && pv.scroll > 0, "reaching the first slider scrolls the list");
    float before = u.cfg[FX_FLANGER].values[0];
    r = fxui_key(&u, BTN_RIGHT, 1, &ch);
    CHECK(u.cfg[FX_FLANGER].values[0] > before && r == FXUI_CHANGED_FX, "right on a slider nudges it with repeat");
    for (int i = 0; i < 100; i++) fxui_key(&u, BTN_RIGHT, 1, &ch);
    CHECK_NEAR(u.cfg[FX_FLANGER].values[0], 5.0f, 1e-4, "slider clamps at max");
    for (int i = 0; i < 20; i++) fxui_key(&u, BTN_DOWN, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(u.row == 12 && pv.scroll == 13 - VISIBLE_ROWS, "down clamps at the last slider, scrolled to the end");
    for (int i = 0; i < 20; i++) fxui_key(&u, BTN_UP, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(u.row == 1 && pv.scroll == 0, "up returns to POWER and scrolls back to the top");
    fxui_key(&u, BTN_B, 0, &ch);
    CHECK(!u.open && u.cfg[FX_FLANGER].enabled, "B closes and the effect stays on");

    /* Keyboard preset page. */
    fxui_key(&u, BTN_UP, 0, &ch); fxui_key(&u, BTN_A, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(u.presetOpen && u.presetRow == 1 && pv.rows[1].selected, "PRESET page opens on the current sound");
    fxui_key(&u, BTN_DOWN, 0, &ch); fxui_key(&u, BTN_DOWN, 0, &ch);
    r = fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.instrument == INST_BELLS && r == FXUI_CHANGED_INST, "A selects GLASS BELLS");
    for (int i = 0; i < 10; i++) fxui_key(&u, BTN_DOWN, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(u.presetRow == 6 && pv.scroll == 1 && strcmp(pv.status, INST_DESCS[5]) == 0, "down clamps at the last sound, scrolls, shows its description");
    fxui_key(&u, BTN_B, 0, &ch);
    CHECK(!u.presetOpen && u.onTopRow, "B closes, focus stays on the top row");

    /* Modes page. */
    fxui_key(&u, BTN_LEFT, 0, &ch); fxui_key(&u, BTN_A, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(u.modesOpen && u.modesRow == 1 && pv.rows[1].selected, "MODES opens on the active preset");
    fxui_key(&u, BTN_DOWN, 0, &ch);
    r = fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.modePreset == 1 && fxui_mode(&u)->rate == 9380 && r == FXUI_CHANGED_MODE, "A on SK-1 selects it");
    for (int i = 0; i < 4; i++) fxui_key(&u, BTN_DOWN, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(pv.rows[u.modesRow].type == ROW_CHOICE && !pv.rows[u.modesRow].editable, "settings rows are locked on SK-1");
    r = fxui_key(&u, BTN_RIGHT, 0, &ch);
    CHECK(fxui_mode(&u)->rate == 9380 && r == 0, "locked: left/right does nothing");
    CHECK(strstr(pv.status, "fixed") != NULL, "status line explains the lock");
    fxui_toggle_loop(&u);
    CHECK(fxui_mode(&u)->loop == 1 && u.loopOverride == 1, "LOOP button turns looping on for SK-1");
    fxui_toggle_loop(&u);
    CHECK(fxui_mode(&u)->loop == 0, "LOOP button toggles back");
    fxui_toggle_loop(&u);
    for (int i = 0; i < 10; i++) fxui_key(&u, BTN_UP, 0, &ch);
    fxui_key(&u, BTN_A, 0, &ch);
    CHECK(u.modePreset == 0 && u.loopOverride == -1, "selecting a preset clears the loop override");
    fxui_toggle_loop(&u);
    CHECK(u.userMode.loop == 0 && u.loopOverride == -1, "on USER the LOOP button edits the USER setting itself");
    for (int i = 0; i < 5; i++) fxui_key(&u, BTN_DOWN, 0, &ch);   /* SK-1, DUSTY, DRONE, (header skipped) LENGTH, RATE */
    fxui_page(&u, &pv);
    CHECK(strcmp(pv.rows[u.modesRow].label, "RATE") == 0 && pv.rows[u.modesRow].editable, "USER: RATE row editable");
    r = fxui_key(&u, BTN_LEFT, 0, &ch);
    CHECK(fxui_mode(&u)->rate == 26040 && r == FXUI_CHANGED_MODE, "left on RATE steps down and reports");
    for (int i = 0; i < 20; i++) fxui_key(&u, BTN_DOWN, 0, &ch);
    fxui_page(&u, &pv);
    CHECK(strcmp(pv.rows[u.modesRow].label, "RELEASE") == 0 && pv.scroll == pv.n - VISIBLE_ROWS, "scrolls to the last setting");
    for (int i = 0; i < 100; i++) fxui_key(&u, BTN_RIGHT, 1, &ch);
    CHECK_NEAR(fxui_mode(&u)->release, 3000, 1e-3, "RELEASE slider repeats and clamps at max");
    fxui_key(&u, BTN_B, 0, &ch);
    CHECK(!u.modesOpen && u.onTopRow, "B closes the modes page, focus stays on the top row");
    fxui_key(&u, BTN_DOWN, 0, &ch);
    CHECK(!u.onTopRow, "down returns to the tab bar");
    MT_END();
}
