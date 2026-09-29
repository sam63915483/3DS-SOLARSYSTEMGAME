/* D-pad navigation for the top screen. Every page is one vertical list: up/down moves through
 * rows (headers are skipped), left/right adjusts the focused row, A selects, B goes back.
 * Six rows fit on screen and the list scrolls to keep the focused row visible.
 * Port of mockup/fxui.js. Pure C. */
#ifndef FXUI_H
#define FXUI_H
#include "fx.h"
#include "voice.h"
#include "sample.h"

enum { BTN_LEFT = 0, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_A, BTN_B };
#define FXUI_CHANGED_FX   1
#define FXUI_CHANGED_INST 2
#define FXUI_CHANGED_MODE 4

enum { ROW_HEADER = 0, ROW_TOGGLE, ROW_PRESET, ROW_CHOICE, ROW_SLIDER };
#define MAX_ROWS 16
#define VISIBLE_ROWS 6

typedef struct {
    int type;
    char label[48];
    char value[24];
    float t;                 /* slider position 0..1 */
    int on, selected, editable, canLeft, canRight, index;
    const char *desc;        /* shown on the status line when focused */
} UiRow;

typedef struct {
    const char *title;
    char right[64];
    UiRow rows[MAX_ROWS];
    int n, focus, scroll;
    char status[112];
} PageView;

typedef struct {
    int tab;            /* highlighted effect tab */
    int open;           /* effect panel showing */
    int row, scroll;    /* focused row index and first visible row */
    FxConfig cfg[FX_COUNT];
    int onTopRow;       /* D-pad focus on the MODES / PRESET buttons */
    int topCol;         /* 0 = MODES, 1 = PRESET */
    int presetOpen, presetRow, presetScroll;
    int instrument;     /* INST_* */
    int modesOpen, modesRow, modesScroll;
    int modePreset;     /* active mode preset, 0 = USER (editable) */
    SampleMode userMode;
    int loopOverride;   /* bottom-screen LOOP button: -1 follow preset, else 0/1 */
    SampleMode effective;
} FxUi;

void fxui_init(FxUi *u);
/* Feeds one button press. repeat = held-key auto repeat. Returns FXUI_CHANGED_* flags;
 * *changedFx receives the effect index when FXUI_CHANGED_FX is set. */
int  fxui_key(FxUi *u, int button, int repeat, int *changedFx);
/* The sampler mode in force: the active preset (or USER) with the LOOP override applied. */
const SampleMode *fxui_mode(FxUi *u);
void fxui_toggle_loop(FxUi *u);
/* Fills the page to draw when a page is open. Returns 0 when no page is open. */
int  fxui_page(const FxUi *u, PageView *pv);

#endif
