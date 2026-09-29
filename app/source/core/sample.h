/* Recorded sample storage and preparation. Port of the trim/loop logic in mockup/sampler.js. */
#ifndef SAMPLE_H
#define SAMPLE_H
#include "config.h"

typedef struct {
    float data[MAX_REC_SAMPLES];
    int length;        /* samples in use */
    int loopStart;     /* playback jumps here after the crossfaded tail */
    int isBuiltIn;     /* 1 for the default tone */
    int trimmedMs;     /* leading silence removed from the last recording */
} Sample;

/* Returns [start, end) with leading/trailing silence removed. Whole range if all quiet. */
void sample_trim_range(const float *data, int n, int *start, int *end);
/* Copies data[0..n) into s, applies start fade + loop crossfade, sets loopStart. */
void sample_set(Sample *s, const float *data, int n, int builtIn);
/* One-second plucked tone at middle C, so the keyboard plays before any recording. */
void sample_make_builtin(Sample *s);
/* Trim + set from a raw recording (int16 mic data converted to float by the caller). */
void sample_from_recording(Sample *s, const float *raw, int n);

#endif
