/* Recorded sample storage, trimming, and the sampler "modes" (length, rate, bits, loop,
 * envelope) that turn a raw recording into the sound the keys play. Port of mockup/sample.js. */
#ifndef SAMPLE_H
#define SAMPLE_H
#include "config.h"

typedef struct {
    float data[MAX_REC_SAMPLES];
    int length;        /* samples in use */
    int loopStart;     /* playback jumps here after the crossfaded tail (loop modes) */
    int rate;          /* Hz the data is stored at (the mode's rate) */
    int loop;          /* 1 = loop while held, 0 = one-shot */
    int isBuiltIn;     /* 1 for the default tone */
    int trimmedMs;     /* leading silence removed from the last recording */
} Sample;

/* seconds: max recording length; rate: stored sample rate; bits: depth; loop: on/off;
 * attack/decay/release in ms, sustain 0..1. */
typedef struct { float seconds; int rate; int bits; int loop; float attack, decay, sustain, release; } SampleMode;

#define MODE_PRESET_COUNT 4
typedef struct { const char *name; int locked; const char *desc; SampleMode settings; } ModePreset;
extern const ModePreset MODE_PRESETS[MODE_PRESET_COUNT];

enum { MF_SECONDS = 0, MF_RATE, MF_BITS, MF_LOOP, MF_ATTACK, MF_DECAY, MF_SUSTAIN, MF_RELEASE, MODE_FIELD_COUNT };
typedef struct {
    const char *label;
    int nChoices;          /* 0 = continuous slider between min and max */
    float choices[6];
    float min, max;
} ModeField;
extern const ModeField MODE_FIELDS[MODE_FIELD_COUNT];

float mode_get(const SampleMode *m, int field);
void  mode_set(SampleMode *m, int field, float v);
float mode_step(int field, float value, int dir);      /* left (-1) / right (+1) on a field */
void  mode_format(int field, float v, char out[24]);

/* Returns [start, end) with leading/trailing silence removed. Whole range if all quiet. */
void sample_trim_range(const float *data, int n, int *start, int *end);
/* Trims and normalises raw (at SR) into out (may not alias). Returns the new length. */
int  sample_trim_normalize(const float *raw, int n, float *out, int *trimmedMs);
/* One-second plucked tone at SR, so the keyboard plays before any recording. Returns length. */
int  sample_builtin_raw(float *out);
/* Runs a raw recording (at SR) through a mode: resample, fades/loop crossfade, quantise. */
void sample_process(Sample *s, const float *raw, int n, const SampleMode *m, int builtIn);

#endif
