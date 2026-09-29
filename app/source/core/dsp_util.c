#include "dsp_util.h"
#include <math.h>
#include <string.h>

void onepole_set(OnePole *f, float hz) {
    float x = expf(-TWO_PI * clampf(hz, 10.0f, SR * 0.45f) / SR);
    f->a = 1.0f - x;
}

void svf_set(SVF *s, float hz, float resonance) {
    hz = clampf(hz, 20.0f, SR * 0.22f);
    s->f = 2.0f * sinf(3.14159265f * hz / (SR * 2.0f));   /* run at 2x internally */
    float q = clampf(resonance, 0.5f, 20.0f);
    s->q = 1.0f / q;
}

void svf_run(SVF *s, float x, float *lp, float *hp, float *bp) {
    float h = 0;
    for (int i = 0; i < 2; i++) {           /* 2x oversampled Chamberlin */
        s->lp += s->f * s->bp;
        h = x - s->lp - s->q * s->bp;
        s->bp += s->f * h;
    }
    /* keep the state bounded if resonance goes wild */
    s->lp = clampf(s->lp, -4.0f, 4.0f); s->bp = clampf(s->bp, -4.0f, 4.0f);
    *lp = s->lp; *hp = h; *bp = s->bp;
}

void lfo_set(LFO *l, float hz) { l->inc = hz / SR; }

void dl_init(DelayLine *d, float *storage, int size) { d->buf = storage; d->size = size; d->w = 0; dl_clear(d); }
void dl_clear(DelayLine *d) { memset(d->buf, 0, d->size * sizeof(float)); }

float dl_read(const DelayLine *d, float delaySamples) {
    delaySamples = clampf(delaySamples, 1.0f, (float)(d->size - 2));
    int di = (int)delaySamples;
    float frac = delaySamples - di;
    int i0 = d->w - di; if (i0 < 0) i0 += d->size;      /* w is the next write slot, so w-1 is 1 sample ago */
    int i1 = i0 - 1; if (i1 < 0) i1 += d->size;
    return d->buf[i0] + (d->buf[i1] - d->buf[i0]) * frac;
}

float fast_tanh(float x) {
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float midi_to_hz(int midi) { return 440.0f * powf(2.0f, (midi - 69) / 12.0f); }
