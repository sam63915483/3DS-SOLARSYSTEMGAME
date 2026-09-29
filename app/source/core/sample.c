#include "sample.h"
#include <math.h>
#include <string.h>

#define TRIM_THRESHOLD 0.02f
#define PRE_ROLL_MS 5
#define TAIL_MS 60
#define LOOP_FADE_MS 50
#define START_FADE_MS 2

void sample_trim_range(const float *data, int n, int *start, int *end) {
    int first = -1, last = -1;
    for (int i = 0; i < n; i++) {
        if (fabsf(data[i]) >= TRIM_THRESHOLD) { if (first < 0) first = i; last = i; }
    }
    if (first < 0) { *start = 0; *end = n; return; }
    *start = first - SR * PRE_ROLL_MS / 1000; if (*start < 0) *start = 0;
    *end = last + SR * TAIL_MS / 1000; if (*end > n) *end = n;
}

void sample_set(Sample *s, const float *data, int n, int builtIn) {
    if (n > MAX_REC_SAMPLES) n = MAX_REC_SAMPLES;
    if (n < 16) { /* nothing usable: keep a tiny silent buffer */
        memset(s->data, 0, 16 * sizeof(float)); s->length = 16; s->loopStart = 0; s->isBuiltIn = builtIn; return;
    }
    memcpy(s->data, data, n * sizeof(float));
    s->length = n;
    s->isBuiltIn = builtIn;
    int startFade = SR * START_FADE_MS / 1000; if (startFade > n / 8) startFade = n / 8;
    for (int i = 0; i < startFade; i++) s->data[i] = data[i] * ((float)i / startFade);
    int fade = SR * LOOP_FADE_MS / 1000; if (fade > n / 4) fade = n / 4;
    int tailStart = n - fade;
    for (int i = 0; i < fade; i++) {
        float t = (float)i / fade;
        s->data[tailStart + i] = data[tailStart + i] * cosf(t * 1.5707963f) + data[i] * sinf(t * 1.5707963f);
    }
    s->loopStart = fade;
}

void sample_make_builtin(Sample *s) {
    static float tone[SR];
    const float f = 261.63f;
    for (int i = 0; i < SR; i++) {
        float t = (float)i / SR;
        float env = (t < 0.005f ? t / 0.005f : 1.0f) * expf(-4.0f * t);
        tone[i] = env * 0.5f * (sinf(6.2831853f * f * t) + 0.5f * sinf(6.2831853f * 2 * f * t) + 0.25f * sinf(6.2831853f * 3 * f * t));
    }
    sample_set(s, tone, SR, 1);
    s->trimmedMs = 0;
}

void sample_from_recording(Sample *s, const float *raw, int n) {
    int start, end;
    sample_trim_range(raw, n, &start, &end);
    s->trimmedMs = start * 1000 / SR;
    sample_set(s, raw + start, end - start, 0);
}
