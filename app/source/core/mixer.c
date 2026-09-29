#include "mixer.h"
#include <math.h>
#include <string.h>

void mixer_init(Mixer *m) {
    memset(m, 0, sizeof(*m));
    sample_make_builtin(&m->sample);
    m->held = -1;
    m->instrument = INST_SAMPLE;
    svf_set(&m->padLow, 7000, 2); svf_set(&m->padHigh, 20, 2);
    fx_init(&m->fx);
    m->master = m->masterTarget = 1.0f;
    m->scopeMidi = -1;
}

static int find_slot(Mixer *m) {
    for (int i = 0; i < MAX_VOICES; i++) if (!m->voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < MAX_VOICES; i++) if (m->voices[i].age < m->voices[oldest].age) oldest = i;
    return oldest;
}

void mixer_note_on(Mixer *m, int midi) {
    if (m->held >= 0) { voice_cut(&m->voices[m->held]); m->held = -1; }
    int slot = find_slot(m);
    voice_note_on(&m->voices[slot], m->instrument, midi, &m->sample, ++m->age);
    voice_bend(&m->voices[slot], m->bend);
    m->held = slot;
}

void mixer_note_off(Mixer *m) {
    if (m->held < 0) return;
    voice_note_off(&m->voices[m->held]);
    m->held = -1;
}

void mixer_set_instrument(Mixer *m, int instrument) { m->instrument = instrument; }

void mixer_set_pad(Mixer *m, float x, float y) {
    m->bend = y * 12.0f;
    m->padX = x;
    if (m->held >= 0) voice_bend(&m->voices[m->held], m->bend);
    float low = x < 0 ? 7000.0f * powf(250.0f / 7000.0f, -x) : 7000.0f;
    float high = x > 0 ? 20.0f * powf(4000.0f / 20.0f, x) : 20.0f;
    svf_set(&m->padLow, low, 2); svf_set(&m->padHigh, high, 2);
}

void mixer_apply_fx(Mixer *m, int fx, const FxConfig *c) { fx_apply(&m->fx, fx, c); }

void mixer_set_muted(Mixer *m, int muted) {
    m->masterTarget = muted ? 0.0f : 1.0f;
    if (muted) { mixer_note_off(m); fx_clear(&m->fx); }
}

void mixer_render(Mixer *m, short *out, int n) {
    static float mono[BLOCK], L[BLOCK], R[BLOCK];
    if (n > BLOCK) n = BLOCK;
    memset(mono, 0, n * sizeof(float));
    for (int i = 0; i < MAX_VOICES; i++) voice_render(&m->voices[i], mono, n);

    /* circle pad filters, bypassed when the pad is centred */
    if (m->padX < -0.02f || m->padX > 0.02f) {
        for (int i = 0; i < n; i++) {
            float lp, hp, bp;
            if (m->padX < 0) { svf_run(&m->padLow, mono[i], &lp, &hp, &bp); mono[i] = lp; }
            else { svf_run(&m->padHigh, mono[i], &lp, &hp, &bp); mono[i] = hp; }
        }
    }
    fx_process(&m->fx, mono, L, R, n);

    /* scope snapshot (post-effects mid signal) */
    double sq = 0; int bad = 0;
    for (int i = 0; i < n; i++) {
        float mid = (L[i] + R[i]) * 0.5f;
        if (!isfinite(mid)) bad = 1;
        m->scope[i] = mid; sq += mid * mid;
    }
    if (bad) {   /* a feedback loop went to infinity/NaN: reset it and stay silent this block */
        fx_clear(&m->fx); memset(m->scope, 0, sizeof(m->scope));
        m->padLow.lp = m->padLow.bp = m->padHigh.lp = m->padHigh.bp = 0;
        memset(out, 0, n * 2 * sizeof(short)); m->nanResets++; m->scopeRms = 0; return;
    }
    m->scopeRms = (float)sqrt(sq / n);
    m->scopeMidi = m->held >= 0 ? m->voices[m->held].midi : -1;

    for (int i = 0; i < n; i++) {
        m->master += (m->masterTarget - m->master) * 0.002f;   /* ~15 ms fade */
        float l = fast_tanh(L[i] * m->master), r = fast_tanh(R[i] * m->master);   /* soft limiter */
        out[2 * i] = (short)(l * 32000.0f);
        out[2 * i + 1] = (short)(r * 32000.0f);
    }
}
