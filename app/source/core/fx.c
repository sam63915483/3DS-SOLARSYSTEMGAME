#include "fx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Same names, ranges and presets as mockup/effects.js (delay/flanger feedback caps included). */
const FxDef FX_DEFS[FX_COUNT] = {
    { "DISTORT", 0,
      { {"DRIVE", 0, 1, 0, '%'}, {"TONE", 300, 12000, 1, 'h'}, {"CRUSH", 0, 1, 0, '%'}, {"MIX", 0, 1, 0, '%'} },
      { {"WARM", MODE_NORMAL, {0.25f, 5000, 0, 0.7f}}, {"CRUNCH", MODE_NORMAL, {0.55f, 3500, 0, 1}},
        {"FUZZ", MODE_NORMAL, {0.95f, 2200, 0, 1}}, {"8-BIT", MODE_NORMAL, {0.15f, 8000, 0.8f, 1}},
        {"DESTROY", MODE_NORMAL, {1, 1200, 0.95f, 1}} } },
    { "CHORUS", 0,
      { {"RATE", 0.1f, 8, 1, 'r'}, {"DEPTH", 0.5f, 12, 0, 'm'}, {"DELAY", 5, 40, 0, 'm'}, {"MIX", 0, 1, 0, '%'} },
      { {"SOFT", MODE_NORMAL, {0.8f, 2, 20, 0.4f}}, {"LUSH", MODE_NORMAL, {0.4f, 5, 25, 0.5f}},
        {"VIBRATO", MODE_NORMAL, {5, 3, 8, 1}}, {"SEASICK", MODE_NORMAL, {0.15f, 12, 30, 0.7f}},
        {"SHIMMER", MODE_NORMAL, {6, 1, 12, 0.5f}} } },
    { "FLANGER", 0,
      { {"RATE", 0.05f, 5, 1, 'r'}, {"DEPTH", 0.2f, 8, 0, 'm'}, {"FEEDBACK", 0, 0.9f, 0, '%'}, {"MIX", 0, 1, 0, '%'} },
      { {"CLASSIC", MODE_NORMAL, {0.3f, 2, 0.5f, 0.5f}}, {"JET", MODE_NORMAL, {0.12f, 5, 0.8f, 0.6f}},
        {"METALLIC", MODE_NORMAL, {2.5f, 1, 0.85f, 0.6f}}, {"SLOW SWEEP", MODE_NORMAL, {0.05f, 8, 0.6f, 0.5f}},
        {"WOBBLE", MODE_NORMAL, {5, 3, 0.3f, 0.5f}} } },
    { "DELAY", 1,
      { {"TIME", 20, 1000, 0, 'm'}, {"FEEDBACK", 0, 0.85f, 0, '%'}, {"TONE", 300, 12000, 1, 'h'}, {"MIX", 0, 1, 0, '%'} },
      { {"NORMAL", MODE_MONO, {350, 0.4f, 6000, 0.4f}}, {"PING PONG", MODE_PINGPONG, {300, 0.5f, 5000, 0.5f}},
        {"SLAPBACK", MODE_MONO, {90, 0.15f, 8000, 0.45f}}, {"DUB", MODE_PINGPONG, {450, 0.6f, 1800, 0.45f}},
        {"CRAZY", MODE_WOBBLE, {120, 0.65f, 4000, 0.5f}} } },
    { "REVERB", 1,
      { {"SIZE", 0.2f, 6, 0, 's'}, {"DAMP", 500, 12000, 1, 'h'}, {"PREDELAY", 0, 120, 0, 'm'}, {"MIX", 0, 1, 0, '%'} },
      { {"ROOM", MODE_NORMAL, {0.7f, 6000, 5, 0.3f}}, {"HALL", MODE_NORMAL, {2.5f, 4000, 20, 0.4f}},
        {"CATHEDRAL", MODE_NORMAL, {6, 3000, 40, 0.5f}}, {"PLATE", MODE_NORMAL, {1.5f, 11000, 0, 0.45f}},
        {"GIANT", MODE_GIANT, {6, 5000, 60, 0.7f}} } },
};

float fx_to_norm(const FxParam *p, float v) {
    if (p->log) return logf(v / p->min) / logf(p->max / p->min);
    return (v - p->min) / (p->max - p->min);
}
float fx_from_norm(const FxParam *p, float t) {
    t = clampf(t, 0, 1);
    if (p->log) return p->min * powf(p->max / p->min, t);
    return p->min + (p->max - p->min) * t;
}
void fx_format_value(const FxParam *p, float v, char out[16]) {
    switch (p->unit) {
        case 's': snprintf(out, 16, "%.2f s", v); break;
        case 'm': snprintf(out, 16, "%d ms", (int)(v + 0.5f)); break;
        case 'r': snprintf(out, 16, "%.2f Hz", v); break;
        case 'h': if (v >= 1000) snprintf(out, 16, "%.1f kHz", v / 1000); else snprintf(out, 16, "%d Hz", (int)(v + 0.5f)); break;
        default:  snprintf(out, 16, "%d%%", (int)(v * 100 + 0.5f)); break;
    }
}
void fx_config_load_preset(int fx, FxConfig *c, int preset) {
    const FxPreset *p = &FX_DEFS[fx].presets[preset];
    c->preset = preset; c->mode = p->mode;
    memcpy(c->values, p->values, sizeof(c->values));
}
void fx_config_default(int fx, FxConfig *c) { c->enabled = 0; fx_config_load_preset(fx, c, 0); }
int fx_config_is_tweaked(int fx, const FxConfig *c) {
    const FxPreset *p = &FX_DEFS[fx].presets[c->preset];
    for (int i = 0; i < FX_PARAMS; i++) if (fabsf(p->values[i] - c->values[i]) > 1e-6f) return 1;
    return 0;
}

/* ---- Audio ---- */

static const int COMB_LEN[REVERB_COMBS] = {1116 * 32728 / 44100, 1188 * 32728 / 44100, 1277 * 32728 / 44100, 1356 * 32728 / 44100};
static const int AP_LEN[REVERB_APS] = {556 * 32728 / 44100, 441 * 32728 / 44100};

void fx_init(FxState *s) {
    memset(s, 0, sizeof(*s));
    dl_init(&s->chorus, s->chorusBuf, CHORUS_MAX);
    dl_init(&s->flanger, s->flangerBuf, FLANGER_MAX);
    dl_init(&s->delayL, s->delayBufL, DELAY_MAX);
    dl_init(&s->delayR, s->delayBufR, DELAY_MAX);
    for (int i = 0; i < REVERB_COMBS; i++) dl_init(&s->comb[i], s->combBuf[i], COMB_LEN[i]);
    for (int i = 0; i < REVERB_APS; i++) dl_init(&s->ap[i], s->apBuf[i], AP_LEN[i]);
    dl_init(&s->pre, s->preBuf, SR / 8);
    onepole_set(&s->tone, 5000); onepole_set(&s->delayToneL, 6000); onepole_set(&s->delayToneR, 6000);
    s->delayTime = 0.3f * SR; s->chorusBase = 0.02f * SR;
    lfo_set(&s->delayLfo, 0.7f);
    FxConfig c;
    for (int i = 0; i < FX_COUNT; i++) { fx_config_default(i, &c); fx_apply(s, i, &c); }
}

void fx_clear(FxState *s) {
    dl_clear(&s->chorus); dl_clear(&s->flanger); dl_clear(&s->delayL); dl_clear(&s->delayR);
    for (int i = 0; i < REVERB_COMBS; i++) { dl_clear(&s->comb[i]); s->combStore[i] = 0; }
    for (int i = 0; i < REVERB_APS; i++) dl_clear(&s->ap[i]);
    dl_clear(&s->pre);
    s->tone.z = s->delayToneL.z = s->delayToneR.z = 0;
}

void fx_apply(FxState *s, int fx, const FxConfig *c) {
    const float *v = c->values;
    float mix = c->enabled ? v[3] : 0;
    s->enabled[fx] = c->enabled;
    switch (fx) {
    case FX_DISTORTION:
        s->drive = 1.0f + v[0] * 40.0f;
        onepole_set(&s->tone, v[1]);
        s->crushLevels = v[2] > 0 ? powf(2.0f, (int)(16 - v[2] * 14 + 0.5f) - 1) : 0;
        s->distMix = mix;
        break;
    case FX_CHORUS: {
        float base = v[2] / 1000.0f, depth = v[1] / 1000.0f;
        if (depth > base - 0.0005f) depth = base - 0.0005f;
        lfo_set(&s->chorusLfo, v[0]);
        s->chorusBase = base * SR; s->chorusDepth = depth * SR; s->chorusMix = mix;
        break; }
    case FX_FLANGER:
        lfo_set(&s->flangerLfo, v[0]);
        s->flangerDepth = v[1] / 1000.0f * SR;
        s->flangerFb = c->enabled ? v[2] : 0;
        s->flangerMix = mix;
        break;
    case FX_DELAY:
        s->delayTime = v[0] / 1000.0f * SR;
        s->delayFb = c->enabled ? v[1] : 0;
        onepole_set(&s->delayToneL, v[2]); onepole_set(&s->delayToneR, v[2]);
        s->delayMix = mix; s->delayMode = c->mode;
        break;
    case FX_REVERB: {
        float size = c->mode == MODE_GIANT ? 6.0f : v[0];
        /* comb feedback so that the tail decays 60 dB in `size` seconds (average comb length) */
        float avgLen = 1234.0f * 32728 / 44100;
        s->revFeedback = powf(0.001f, avgLen / (size * SR));
        if (s->revFeedback > 0.985f) s->revFeedback = 0.985f;
        s->revDamp = 1.0f - clampf(v[1] / 12000.0f, 0.05f, 1.0f);
        s->preTime = v[2] / 1000.0f * SR;
        s->revMix = mix;
        break; }
    }
}

static inline float distort(FxState *s, float x) {
    float y = fast_tanh(x * s->drive) / fast_tanh(s->drive);
    if (s->crushLevels > 0) y = floorf(y * s->crushLevels + 0.5f) / s->crushLevels;
    y *= 1.0f - (s->drive - 1.0f) / 40.0f * 0.35f;
    return onepole_run(&s->tone, y);
}

void fx_process(FxState *s, const float *in, float *outL, float *outR, int n) {
    for (int i = 0; i < n; i++) {
        float x = in[i];
        /* distortion */
        if (s->enabled[FX_DISTORTION]) { float w = distort(s, x); x = x * (1 - s->distMix) + w * s->distMix; }
        /* chorus */
        if (s->enabled[FX_CHORUS]) {
            float d = s->chorusBase + s->chorusDepth * lfo_run(&s->chorusLfo);
            float w = dl_read(&s->chorus, d);
            dl_write(&s->chorus, x);
            x = x * (1 - s->chorusMix) + w * s->chorusMix;
        } else dl_write(&s->chorus, x);
        /* flanger */
        if (s->enabled[FX_FLANGER]) {
            float d = 0.0005f * SR + s->flangerDepth * (1.0f + lfo_run(&s->flangerLfo));
            float w = dl_read(&s->flanger, d);
            dl_write(&s->flanger, x + w * s->flangerFb);
            x = x * (1 - s->flangerMix) + w * s->flangerMix;
        } else dl_write(&s->flanger, x);
        /* delay -> stereo */
        float L = x, R = x;
        if (s->enabled[FX_DELAY]) {
            float t = s->delayTime;
            if (s->delayMode == MODE_WOBBLE) t += s->delayTime * 0.3f * lfo_run(&s->delayLfo);
            float dl = onepole_run(&s->delayToneL, dl_read(&s->delayL, t));
            float dr = onepole_run(&s->delayToneR, dl_read(&s->delayR, t));
            if (s->delayMode == MODE_PINGPONG) {
                dl_write(&s->delayL, x + dr * s->delayFb);
                dl_write(&s->delayR, dl * s->delayFb);
                L = x + (dl * 0.9f + dr * 0.1f) * s->delayMix;
                R = x + (dr * 0.9f + dl * 0.1f) * s->delayMix;
            } else {
                dl_write(&s->delayL, x + dl * s->delayFb);
                dl_write(&s->delayR, 0);
                L = R = x + dl * s->delayMix;
            }
        } else { dl_write(&s->delayL, 0); dl_write(&s->delayR, 0); }
        /* reverb (mono Freeverb-lite on the mid signal, added to both sides) */
        if (s->enabled[FX_REVERB]) {
            float mid = (L + R) * 0.5f;
            dl_write(&s->pre, mid);
            float src = dl_read(&s->pre, s->preTime + 1);
            float acc = 0;
            for (int c = 0; c < REVERB_COMBS; c++) {
                float out = dl_read(&s->comb[c], (float)(s->comb[c].size - 2));
                s->combStore[c] = out * (1 - s->revDamp) + s->combStore[c] * s->revDamp;
                dl_write(&s->comb[c], src + s->combStore[c] * s->revFeedback);
                acc += out;
            }
            acc *= 0.25f;
            for (int a = 0; a < REVERB_APS; a++) {
                float bufout = dl_read(&s->ap[a], (float)(s->ap[a].size - 2));
                float y = -acc + bufout;
                dl_write(&s->ap[a], acc + bufout * 0.5f);
                acc = y;
            }
            L += acc * s->revMix; R += acc * s->revMix;
        }
        outL[i] = L; outR[i] = R;
    }
}
