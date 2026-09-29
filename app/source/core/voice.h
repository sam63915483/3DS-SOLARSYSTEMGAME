/* One playing note: either the recorded sample (rate-pitched, looped or one-shot, shaped by
 * the mode's ADSR) or one of the five synth instruments. Port of sampler.js / instruments.js. */
#ifndef VOICE_H
#define VOICE_H
#include "config.h"
#include "dsp_util.h"
#include "sample.h"

enum { INST_SAMPLE = 0, INST_DREAM, INST_BELLS, INST_AURORA, INST_SUPERSAW, INST_WOBBLE, INST_COUNT };
extern const char *INST_NAMES[INST_COUNT];
extern const char *INST_DESCS[INST_COUNT];

#define MAX_OSC 9

typedef struct { float phase, hz, detune, extraDetune; int type; float gain; } Osc;  /* type: 0 sine 1 saw 2 square 3 tri */

typedef struct {
    int active, releasing;
    int instrument, midi;
    float bend;                 /* semitones from the circle pad */
    /* amplitude envelope: stage 0 attack, 1 hold/decay, 2 release */
    int stage;
    float amp, peak, attackInc, releaseCoef, holdTarget, holdCoef;
    /* sample playback */
    const Sample *sample; double pos;
    /* synth parts */
    Osc osc[MAX_OSC]; int nOsc;
    SVF f1, f2, f3; OnePole tone;
    LFO lfo1, lfo2, lfo3;
    float t;                    /* seconds since note on */
    float fmIndex, fmTarget, fmCoef;
    float pitchDrop, pitchDropCoef;
    float cutStart, cutEnd, cutTime;
    unsigned age;               /* for voice stealing */
} Voice;

/* mode is only used for INST_SAMPLE (its envelope); pass NULL for synths. */
void voice_note_on(Voice *v, int instrument, int midi, const Sample *sample, const SampleMode *mode, unsigned age);
void voice_note_off(Voice *v);                  /* natural release */
void voice_cut(Voice *v);                       /* quick 30 ms release (retrigger) */
void voice_bend(Voice *v, float semitones);
/* Adds n samples into out[] (mixing). Deactivates itself when the release finishes. */
void voice_render(Voice *v, float *out, int n);

#endif
