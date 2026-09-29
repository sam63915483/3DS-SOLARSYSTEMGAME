#include "voice.h"
#include <math.h>
#include <string.h>

const char *INST_NAMES[INST_COUNT] = {"YOUR SAMPLE", "DREAM PAD", "GLASS BELLS", "AURORA", "SUPERSAW", "WOBBLE BASS"};
const char *INST_DESCS[INST_COUNT] = {
    "The sound you recorded, pitched across the keys",
    "Slow, wide, detuned pad that blooms as you hold it",
    "Bright FM bell tones with a long shimmering tail",
    "Breathy choir-like voices drifting through vowels",
    "Seven detuned saws, punchy EDM lead",
    "Growling filter-wobble bass, an octave down",
};

/* ---- helpers ---- */
static float fast_sin01(float p) {           /* p in 0..1 -> approx sin(2*pi*p) */
    float x = p < 0.5f ? p * 4.0f - 1.0f : 3.0f - p * 4.0f;
    return x * (2.0f - fabsf(x));
}
static inline float osc_run(Osc *o, float bendSemis) {
    float cents = o->detune + o->extraDetune + bendSemis * 100.0f;
    float hz = o->hz * powf(2.0f, cents / 1200.0f);
    o->phase += hz / SR; if (o->phase >= 1.0f) o->phase -= 1.0f;
    float p = o->phase;
    switch (o->type) {
        case 1: return (2.0f * p - 1.0f) * o->gain;
        case 2: return (p < 0.5f ? 1.0f : -1.0f) * o->gain;
        case 3: return (p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p) * o->gain;
        default: return fast_sin01(p) * o->gain;
    }
}
static void add_osc(Voice *v, int type, float hz, float detune, float gain) {
    if (v->nOsc >= MAX_OSC) return;
    Osc *o = &v->osc[v->nOsc++];
    o->type = type; o->hz = hz; o->detune = detune; o->extraDetune = 0; o->gain = gain; o->phase = 0;
}
static void env_setup(Voice *v, float attack, float release, float peak) {
    v->amp = 0; v->peak = peak;
    v->attackInc = peak / (attack * SR);
    v->releaseCoef = expf(-1.0f / (release / 4.0f * SR));
    v->holdTarget = peak; v->holdCoef = 0;
}
static inline float env_run(Voice *v) {
    if (v->releasing) v->amp *= v->releaseCoef;
    else if (v->amp < v->peak && v->holdCoef == 0) { v->amp += v->attackInc; if (v->amp > v->peak) v->amp = v->peak; }
    else if (v->holdCoef > 0) v->amp += (v->holdTarget - v->amp) * v->holdCoef;
    return v->amp;
}

void voice_note_on(Voice *v, int instrument, int midi, const Sample *sample, unsigned age) {
    memset(v, 0, sizeof(*v));
    v->active = 1; v->instrument = instrument; v->midi = midi; v->age = age;
    float f = midi_to_hz(midi);
    switch (instrument) {
    case INST_SAMPLE:
        v->sample = sample; v->pos = 0;
        env_setup(v, 0.004f, 0.04f, 1.0f);
        break;
    case INST_DREAM:
        add_osc(v, 1, f, -9, 0.22f); add_osc(v, 1, f, 9, 0.22f); add_osc(v, 3, f, 0, 0.22f); add_osc(v, 0, f / 2, 0, 0.11f);
        svf_set(&v->f1, 400, 1.2f); v->cutStart = 400; v->cutEnd = 2400; v->cutTime = 0.8f;
        lfo_set(&v->lfo1, 0.35f);
        env_setup(v, 0.5f, 1.6f, 0.45f);
        break;
    case INST_BELLS:
        add_osc(v, 0, f, 0, 1.0f);            /* carrier */
        add_osc(v, 0, f * 3.5f, 0, 1.0f);     /* modulator */
        add_osc(v, 0, f * 2, 6, 0.18f);       /* shimmer */
        v->fmIndex = 2.5f; v->fmTarget = 0.15f; v->fmCoef = 1.0f / (0.35f * SR);
        env_setup(v, 0.005f, 1.8f, 0.5f);
        v->holdTarget = 0.12f; v->holdCoef = 1.0f / (1.2f * SR);   /* decays while held */
        break;
    case INST_AURORA:
        add_osc(v, 3, f, -5, 0.4f); add_osc(v, 3, f, 5, 0.4f); add_osc(v, 1, f, 0, 0.12f);
        svf_set(&v->f1, 700, 6); svf_set(&v->f2, 1200, 6); svf_set(&v->f3, 900, 0.7f);
        lfo_set(&v->lfo1, 0.18f); lfo_set(&v->lfo2, 0.23f); lfo_set(&v->lfo3, 5.0f);
        env_setup(v, 0.9f, 2.2f, 0.9f);
        break;
    case INST_SUPERSAW: {
        static const float det[7] = {-36, -24, -12, 0, 12, 24, 36};
        for (int i = 0; i < 7; i++) add_osc(v, 1, f, det[i], 0.13f);
        add_osc(v, 2, f / 2, 0, 0.13f * 0.2f);
        v->pitchDrop = 250; v->pitchDropCoef = 1.0f / (0.03f * SR);
        svf_set(&v->f1, 7000, 1.5f); svf_set(&v->f2, 7000, 0.7f);
        v->cutStart = 7000; v->cutEnd = 3000; v->cutTime = 0.25f;
        env_setup(v, 0.01f, 0.18f, 0.42f);
        break; }
    case INST_WOBBLE:
        add_osc(v, 1, f / 2, 0, 0.3f); add_osc(v, 2, f / 2, 7, 0.3f); add_osc(v, 0, f / 4, 0, 0.15f);
        svf_set(&v->f1, 900, 9); lfo_set(&v->lfo1, 4.67f);
        env_setup(v, 0.01f, 0.12f, 0.45f);
        break;
    }
}

void voice_note_off(Voice *v) { v->releasing = 1; }
void voice_cut(Voice *v) { v->releasing = 1; v->releaseCoef = expf(-1.0f / (0.0075f * SR)); }
void voice_bend(Voice *v, float semitones) { v->bend = semitones; }

static inline float render_synth(Voice *v) {
    float x = 0, lp, hp, bp;
    switch (v->instrument) {
    case INST_DREAM: {
        float vib = lfo_run(&v->lfo1) * 6.0f;
        for (int i = 0; i < 3; i++) v->osc[i].extraDetune = vib;
        for (int i = 0; i < v->nOsc; i++) x += osc_run(&v->osc[i], v->bend);
        float cut = v->t >= v->cutTime ? v->cutEnd : v->cutStart * powf(v->cutEnd / v->cutStart, v->t / v->cutTime);
        svf_set(&v->f1, cut, 1.2f);
        svf_run(&v->f1, x, &lp, &hp, &bp);
        return lp;
    }
    case INST_BELLS: {
        v->fmIndex += (v->fmTarget - v->fmIndex) * v->fmCoef;
        float mod = osc_run(&v->osc[1], v->bend);
        Osc *c = &v->osc[0];
        float hz = c->hz * powf(2.0f, v->bend / 12.0f);
        c->phase += (hz + mod * hz * v->fmIndex) / SR;
        while (c->phase >= 1.0f) c->phase -= 1.0f;
        while (c->phase < 0.0f) c->phase += 1.0f;
        return fast_sin01(c->phase) + osc_run(&v->osc[2], v->bend);
    }
    case INST_AURORA: {
        float vib = lfo_run(&v->lfo3) * 4.0f;
        v->osc[0].extraDetune = vib; v->osc[1].extraDetune = vib;
        for (int i = 0; i < v->nOsc; i++) x += osc_run(&v->osc[i], v->bend);
        svf_set(&v->f1, 700 + 320 * lfo_run(&v->lfo1), 6);
        svf_set(&v->f2, 1200 + 420 * lfo_run(&v->lfo2), 6);
        float a, b, body;
        svf_run(&v->f1, x, &lp, &hp, &a);
        svf_run(&v->f2, x, &lp, &hp, &b);
        svf_run(&v->f3, x, &body, &hp, &bp);
        return a + b + body * 0.35f;
    }
    case INST_SUPERSAW: {
        v->pitchDrop -= v->pitchDrop * v->pitchDropCoef;
        for (int i = 0; i < 7; i++) v->osc[i].extraDetune = v->pitchDrop;
        for (int i = 0; i < v->nOsc; i++) x += osc_run(&v->osc[i], v->bend);
        float cut = v->t >= v->cutTime ? v->cutEnd : v->cutStart * powf(v->cutEnd / v->cutStart, v->t / v->cutTime);
        svf_set(&v->f1, cut, 1.5f); svf_set(&v->f2, cut, 0.7f);
        svf_run(&v->f1, x, &lp, &hp, &bp);
        svf_run(&v->f2, lp, &lp, &hp, &bp);
        return lp;
    }
    case INST_WOBBLE: {
        for (int i = 0; i < v->nOsc; i++) x += osc_run(&v->osc[i], v->bend);
        svf_set(&v->f1, 900 + 700 * lfo_run(&v->lfo1), 9);
        svf_run(&v->f1, x, &lp, &hp, &bp);
        return fast_tanh(lp * 3.0f);
    }
    }
    return 0;
}

void voice_render(Voice *v, float *out, int n) {
    if (!v->active) return;
    if (v->instrument == INST_SAMPLE) {
        const Sample *s = v->sample;
        double rate = pow(2.0, (v->midi - ROOT_MIDI + v->bend) / 12.0);
        for (int i = 0; i < n; i++) {
            int i0 = (int)v->pos; float frac = (float)(v->pos - i0);
            int i1 = i0 + 1; if (i1 >= s->length) i1 = s->loopStart;
            float x = s->data[i0] + (s->data[i1] - s->data[i0]) * frac;
            out[i] += x * env_run(v);
            v->pos += rate;
            while (v->pos >= s->length) v->pos -= (s->length - s->loopStart);
        }
    } else {
        for (int i = 0; i < n; i++) {
            out[i] += render_synth(v) * env_run(v);
            v->t += 1.0f / SR;
        }
    }
    if (v->releasing && v->amp < 1e-3f) v->active = 0;   /* -60 dB: inaudible, frees the slot */
}
