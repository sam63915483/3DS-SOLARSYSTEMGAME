/* Small DSP building blocks shared by the effects, synths and mixer. Pure C. */
#ifndef DSP_UTIL_H
#define DSP_UTIL_H
#include "config.h"

#define TWO_PI 6.28318530718f

/* One-pole low-pass (cheap tone controls). */
typedef struct { float a, z; } OnePole;
void  onepole_set(OnePole *f, float hz);
static inline float onepole_run(OnePole *f, float x) { f->z += f->a * (x - f->z); return f->z; }

/* Chamberlin state-variable filter: lp / hp / bp outputs. Stable up to ~SR/6 with 2x oversampling. */
typedef struct { float f, q, lp, bp; } SVF;
void  svf_set(SVF *s, float hz, float resonance /* 0.5 .. 20 */);
void  svf_run(SVF *s, float x, float *lp, float *hp, float *bp);

/* Sine LFO. */
typedef struct { float phase, inc; } LFO;
void  lfo_set(LFO *l, float hz);
static inline float lfo_run(LFO *l) {
    l->phase += l->inc; if (l->phase >= 1.0f) l->phase -= 1.0f;
    float p = l->phase;  /* fast sine approximation, good enough for modulation */
    float x = p < 0.5f ? p * 4.0f - 1.0f : 3.0f - p * 4.0f;   /* triangle -1..1 */
    return x * (2.0f - (x < 0 ? -x : x));                    /* softened toward a sine */
}

/* Delay line with fractional read. */
typedef struct { float *buf; int size, w; } DelayLine;
void  dl_init(DelayLine *d, float *storage, int size);
void  dl_clear(DelayLine *d);
static inline void dl_write(DelayLine *d, float x) { d->buf[d->w] = x; d->w = (d->w + 1) % d->size; }
float dl_read(const DelayLine *d, float delaySamples);   /* linear interpolation */

float fast_tanh(float x);
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float midi_to_hz(int midi);

#endif
