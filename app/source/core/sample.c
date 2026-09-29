#include "sample.h"
#include <math.h>
#include <string.h>

/* Trimming works on a 10 ms RMS envelope. The sound is taken to start where the envelope
 * first reaches a level relative to the loudest moment (with an absolute floor so faint
 * noise never counts) and to end where it last stays above a lower "decay" level. */
#define WIN (SR / 100)               /* 10 ms windows */
#define MAX_WINDOWS (MAX_REC_SAMPLES / WIN + 1)
#define ABS_FLOOR 0.04f              /* RMS below this is always silence (about -28 dBFS) */
#define START_REL 0.15f              /* start: within -16 dB of the loudest window */
#define END_REL 0.03f                /* end: last window within -30 dB of the loudest */
#define PRE_ROLL_MS 10
#define TAIL_MS 60
#define LOOP_FADE_MS 50
#define START_FADE_MS 2
#define NORMALIZE_PEAK 0.9f
#define MAX_GAIN 20.0f

static float envBuf[MAX_WINDOWS];

void sample_trim_range(const float *data, int n, int *start, int *end) {
    int nw = n / WIN;
    if (nw < 1) { *start = 0; *end = n; return; }
    float peak = 0;
    for (int w = 0; w < nw; w++) {
        double sq = 0;
        for (int i = w * WIN; i < (w + 1) * WIN; i++) sq += data[i] * data[i];
        envBuf[w] = sqrtf((float)(sq / WIN));
        if (envBuf[w] > peak) peak = envBuf[w];
    }
    if (peak < ABS_FLOOR) { *start = 0; *end = n; return; }   /* nothing meaningful: keep all */
    float thrStart = peak * START_REL; if (thrStart < ABS_FLOOR) thrStart = ABS_FLOOR;
    float thrEnd = peak * END_REL; if (thrEnd < ABS_FLOOR * 0.5f) thrEnd = ABS_FLOOR * 0.5f;
    int first = 0; while (first < nw && envBuf[first] < thrStart) first++;
    int last = nw - 1; while (last > first && envBuf[last] < thrEnd) last--;
    *start = first * WIN - SR * PRE_ROLL_MS / 1000; if (*start < 0) *start = 0;
    *end = (last + 1) * WIN + SR * TAIL_MS / 1000; if (*end > n) *end = n;
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
    /* Normalise so quiet recordings play at full level. */
    float peak = 0;
    for (int i = 0; i < s->length; i++) { float a = fabsf(s->data[i]); if (a > peak) peak = a; }
    if (peak > 1e-4f) {
        float gain = NORMALIZE_PEAK / peak; if (gain > MAX_GAIN) gain = MAX_GAIN;
        for (int i = 0; i < s->length; i++) s->data[i] *= gain;
    }
}
