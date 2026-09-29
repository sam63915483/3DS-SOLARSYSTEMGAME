/* D-pad navigation for the top screen: effect tab bar and panel, the MODES button/page (top
 * left) and the keyboard PRESET button/page (top right). Port of mockup/fxui.js. Pure C. */
#ifndef FXUI_H
#define FXUI_H
#include "fx.h"
#include "voice.h"
#include "sample.h"

enum { BTN_LEFT = 0, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_A, BTN_B };
#define FXUI_CHANGED_FX   1
#define FXUI_CHANGED_INST 2
#define FXUI_CHANGED_MODE 4

typedef struct {
    int tab;            /* highlighted effect tab */
    int open;           /* effect panel showing */
    int row, col;       /* focus in panel: row 0 power, 1 presets (col), 2.. sliders */
    FxConfig cfg[FX_COUNT];
    /* top row of buttons: D-pad up from the tab bar focuses it, left/right picks MODES or PRESET */
    int onTopRow;
    int topCol;         /* 0 = MODES, 1 = PRESET */
    /* keyboard preset page */
    int presetOpen;
    int presetRow;
    int instrument;     /* INST_* */
    /* modes page */
    int modesOpen;
    int modesRow;       /* 0 = preset row, 1.. = fields */
    int modesCol;       /* highlighted mode preset when modesRow == 0 */
    int modePreset;     /* active mode preset, 0 = USER (editable) */
    SampleMode userMode;
} FxUi;

void fxui_init(FxUi *u);
/* Feeds one button press. repeat = held-key auto repeat. Returns FXUI_CHANGED_* flags;
 * *changedFx receives the effect index when FXUI_CHANGED_FX is set. */
int  fxui_key(FxUi *u, int button, int repeat, int *changedFx);
const SampleMode *fxui_mode(const FxUi *u);

#endif
