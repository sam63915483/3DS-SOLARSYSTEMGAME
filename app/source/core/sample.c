#include "sample.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Modes ------------------------------------------------------------------------------ */

const ModePreset MODE_PRESETS[MODE_PRESET_COUNT] = {
    { "USER", 0, "Editable. Starts as the default: full quality, looping",
      { 10, 32728, 16, 1, 4, 0, 1.0f, 40 } },
    { "SK-1", 1, "1.4 s, 9.4 kHz, 8-bit, one-shot, organ envelope: the real thing",
      { 1.4f, 9380, 8, 0, 0, 0, 1.0f, 20 } },
    { "DUSTY", 1, "12-bit 26 kHz one-shot with a punchy decay, 80s drum-sampler grit",
      { 5, 26040, 12, 0, 0, 400, 0.6f, 150 } },
    { "DRONE", 1, "Full quality, looping, slow swell and a long release for pads",
      { 10, 32728, 16, 1, 800, 0, 1.0f, 2000 } },
};

const ModeField MODE_FIELDS[MODE_FIELD_COUNT] = {
    { "LENGTH",  4, {1.4f, 2, 5, 10}, 0, 0 },
    { "RATE",    6, {8000, 9380, 16000, 22050, 26040, 32728}, 0, 0 },
    { "BITS",    4, {16, 12, 8, 4}, 0, 0 },
    { "LOOP",    2, {1, 0}, 0, 0 },
    { "ATTACK",  0, {0}, 0, 1000 },
    { "DECAY",   0, {0}, 0, 2000 },
    { "SUSTAIN", 0, {0}, 0, 1 },
    { "RELEASE", 0, {0}, 5, 3000 },
};

float mode_get(const SampleMode *m, int f) {
    switch (f) {
        case MF_SECONDS: return m->seconds; case MF_RATE: return (float)m->rate; case MF_BITS: return (float)m->bits;
        case MF_LOOP: return (float)m->loop; case MF_ATTACK: return m->attack; case MF_DECAY: return m->decay;
        case MF_SUSTAIN: return m->sustain; default: return m->release;
    }
}
void mode_set(SampleMode *m, int f, float v) {
    switch (f) {
        case MF_SECONDS: m->seconds = v; break; case MF_RATE: m->rate = (int)v; break; case MF_BITS: m->bits = (int)v; break;
        case MF_LOOP: m->loop = v != 0; break; case MF_ATTACK: m->attack = v; break; case MF_DECAY: m->decay = v; break;
        case MF_SUSTAIN: m->sustain = v; break; default: m->release = v; break;
    }
}
float mode_step(int f, float value, int dir) {
    const ModeField *fd = &MODE_FIELDS[f];
    if (fd->nChoices) {
        int i = 0;
        for (int k = 0; k < fd->nChoices; k++) if (fabsf(fd->choices[k] - value) < 1e-3f) i = k;
        i += dir; if (i < 0) i = 0; if (i >= fd->nChoices) i = fd->nChoices - 1;
        return fd->choices[i];
    }
    float step = (fd->max - fd->min) * 0.025f;
    value += dir * step;
    if (value < fd->min) value = fd->min;
    if (value > fd->max) value = fd->max;
    return value;
}
void mode_format(int f, float v, char out[24]) {
    switch (f) {
        case MF_SECONDS: snprintf(out, 24, "%g s", v); break;
        case MF_RATE: snprintf(out, 24, "%.1f kHz", v / 1000); break;
        case MF_BITS: snprintf(out, 24, "%d-bit", (int)v); break;
        case MF_LOOP: snprintf(out, 24, "%s", v != 0 ? "ON" : "OFF (one-shot)"); break;
        case MF_SUSTAIN: snprintf(out, 24, "%d%%", (int)(v * 100 + 0.5f)); break;
        default: snprintf(out, 24, "%d ms", (int)(v + 0.5f)); break;
    }
}

/* ---- Trimming --------------------------------------------------------------------------- */
/* Works on a 10 ms RMS envelope. The sound is taken to start where the envelope first reaches
 * a level relative to the loudest moment (with an absolute floor so faint noise never counts)
 * and to end where it last stays above a lower "decay" level. */
#define WIN (SR / 100)
#define MAX_WINDOWS (MAX_REC_SAMPLES / WIN + 1)
#define ABS_FLOOR 0.04f
#define START_REL 0.15f
#define END_REL 0.03f
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
    if (peak < ABS_FLOOR) { *start = 0; *end = n; return; }
    float thrStart = peak * START_REL; if (thrStart < ABS_FLOOR) thrStart = ABS_FLOOR;
    float thrEnd = peak * END_REL; if (thrEnd < ABS_FLOOR * 0.5f) thrEnd = ABS_FLOOR * 0.5f;
    int first = 0; while (first < nw && envBuf[first] < thrStart) first++;
    int last = nw - 1; while (last > first && envBuf[last] < thrEnd) last--;
    *start = first * WIN - SR * PRE_ROLL_MS / 1000; if (*start < 0) *start = 0;
    *end = (last + 1) * WIN + SR * TAIL_MS / 1000; if (*end > n) *end = n;
}

int sample_trim_normalize(const float *raw, int n, float *out, int *trimmedMs) {
    int start, end;
    sample_trim_range(raw, n, &start, &end);
    *trimmedMs = start * 1000 / SR;
    int len = end - start;
    if (len > MAX_REC_SAMPLES) len = MAX_REC_SAMPLES;
    float peak = 0;
    for (int i = 0; i < len; i++) { float a = fabsf(raw[start + i]); if (a > peak) peak = a; }
    float gain = 1.0f;
    if (peak > 1e-4f) { gain = NORMALIZE_PEAK / peak; if (gain > MAX_GAIN) gain = MAX_GAIN; }
    for (int i = 0; i < len; i++) out[i] = raw[start + i] * gain;
    return len;
}

int sample_builtin_raw(float *out) {
    const float f = 261.63f;
    for (int i = 0; i < SR; i++) {
        float t = (float)i / SR;
        float env = (t < 0.005f ? t / 0.005f : 1.0f) * expf(-4.0f * t);
        out[i] = env * 0.5f * (sinf(6.2831853f * f * t) + 0.5f * sinf(6.2831853f * 2 * f * t) + 0.25f * sinf(6.2831853f * 3 * f * t));
    }
    return SR;
}

/* Resample (linear, aliasing and all: that's the lo-fi charm), fade the start, crossfade the
 * loop seam when looping, then quantise last so the stored data really is N-bit. */
void sample_process(Sample *s, const float *raw, int n, const SampleMode *m, int builtIn) {
    s->rate = m->rate; s->loop = m->loop; s->isBuiltIn = builtIn;
    double ratio = (double)SR / m->rate;
    int len = (int)(n / ratio);
    if (len > MAX_REC_SAMPLES) len = MAX_REC_SAMPLES;
    if (n < 2 || len < 16) {
        memset(s->data, 0, 16 * sizeof(float)); s->length = 16; s->loopStart = 0; return;
    }
    for (int i = 0; i < len; i++) {
        double p = i * ratio;
        int i0 = (int)p; if (i0 > n - 2) i0 = n - 2;
        float f = (float)(p - i0);
        s->data[i] = raw[i0] + (raw[i0 + 1] - raw[i0]) * f;
    }
    s->length = len;
    int startFade = m->rate * START_FADE_MS / 1000; if (startFade > len / 8) startFade = len / 8;
    for (int i = 0; i < startFade; i++) s->data[i] *= (float)i / startFade;
    if (m->loop) {
        int fade = m->rate * LOOP_FADE_MS / 1000; if (fade > len / 4) fade = len / 4;
        int tailStart = len - fade;
        for (int i = 0; i < fade; i++) {
            float t = (float)i / fade;
            s->data[tailStart + i] = s->data[tailStart + i] * cosf(t * 1.5707963f) + s->data[i] * sinf(t * 1.5707963f);
        }
        s->loopStart = fade;
    } else s->loopStart = 0;
    if (m->bits < 16) {
        float levels = powf(2.0f, (float)(m->bits - 1));
        for (int i = 0; i < len; i++) s->data[i] = roundf(s->data[i] * levels) / levels;
    }
}
