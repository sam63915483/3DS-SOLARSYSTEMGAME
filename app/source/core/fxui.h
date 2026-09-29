/* D-pad navigation for the top screen: effect tab bar, effect panel, keyboard preset button
 * and page. Port of the state machine in mockup/fxui.js. Pure C. */
#ifndef FXUI_H
#define FXUI_H
#include "fx.h"
#include "voice.h"

enum { BTN_LEFT = 0, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_A, BTN_B };
#define FXUI_CHANGED_FX   1
#define FXUI_CHANGED_INST 2

typedef struct {
    int tab;            /* highlighted effect tab */
    int open;           /* effect panel showing */
    int row, col;       /* focus in panel: row 0 power, 1 presets (col), 2.. sliders */
    FxConfig cfg[FX_COUNT];
    int onPresetButton; /* focus on the top-right keyboard preset button */
    int presetOpen;     /* keyboard preset page showing */
    int presetRow;
    int instrument;     /* INST_* */
} FxUi;

void fxui_init(FxUi *u);
/* Feeds one button press. repeat = held-key auto repeat. Returns FXUI_CHANGED_* flags;
 * *changedFx receives the effect index when FXUI_CHANGED_FX is set. */
int  fxui_key(FxUi *u, int button, int repeat, int *changedFx);

#endif
