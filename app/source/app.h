/* Whole-app state shared by the 3DS-side files. */
#ifndef APP_H
#define APP_H
#include "mixer.h"
#include "keyboard.h"
#include "fxui.h"

enum { TOP_SCOPE = 0, TOP_RECORDING, TOP_SAMPLE };

typedef struct {
    Mixer mixer;
    Keyboard kb;
    FxUi fxui;
    int heldMidi;          /* key under the stylus or -1 */
    int drag;              /* 0 none, 1 key glide, 2 slider */
    int grabOffset;
    int topMode;
    int dspOk, micOk;
    /* waveform of the current sample for the top screen, one min/max per column */
    float peakMin[WAVE_COLUMNS], peakMax[WAVE_COLUMNS];
} App;

void app_compute_sample_peaks(App *a);

#endif
