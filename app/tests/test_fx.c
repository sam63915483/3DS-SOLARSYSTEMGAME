#include "minitest.h"
#include "fx.h"
#include <stdlib.h>
#include <string.h>

static FxState S;   /* large: keep off the stack */

static int all_finite(const float *a, int n) { for (int i = 0; i < n; i++) if (!isfinite(a[i])) return 0; return 1; }

int main(void) {
    char txt[16];
    for (int f = 0; f < FX_COUNT; f++) {
        for (int p = 0; p < FX_PRESETS; p++) for (int k = 0; k < FX_PARAMS; k++) {
            float v = FX_DEFS[f].presets[p].values[k];
            if (v < FX_DEFS[f].params[k].min - 1e-6f || v > FX_DEFS[f].params[k].max + 1e-6f) {
                printf("  %s / %s / %s = %g out of range\n", FX_DEFS[f].name, FX_DEFS[f].presets[p].name, FX_DEFS[f].params[k].label, v);
                CHECK(0, "preset value in range");
            }
        }
    }
    CHECK(1, "all preset values inside their slider ranges (see above for any failures)");
    const FxParam *hz = &FX_DEFS[FX_REVERB].params[1];
    CHECK_NEAR(fx_from_norm(hz, fx_to_norm(hz, 4000)), 4000, 1e-2, "log param round-trips");
    fx_format_value(hz, 4000, txt); CHECK(strcmp(txt, "4.0 kHz") == 0, "kHz formatting");
    fx_format_value(&FX_DEFS[FX_REVERB].params[3], 0.3f, txt); CHECK(strcmp(txt, "30%") == 0, "percent formatting");

    fx_init(&S);
    float in[BLOCK], L[BLOCK], R[BLOCK];
    for (int i = 0; i < BLOCK; i++) in[i] = 0.5f * sinf(TWO_PI * 440 * i / SR);
    fx_process(&S, in, L, R, BLOCK);
    double diff = 0; for (int i = 0; i < BLOCK; i++) diff += fabs(L[i] - in[i]) + fabs(R[i] - in[i]);
    CHECK(diff < 1e-6, "all effects off = bit-exact pass-through");

    /* Delay: one echo at the set time, no feedback. */
    FxConfig c; fx_config_default(FX_DELAY, &c); c.enabled = 1; c.mode = MODE_MONO;
    c.values[0] = 100; c.values[1] = 0; c.values[2] = 12000; c.values[3] = 1.0f;
    fx_apply(&S, FX_DELAY, &c);
    float imp[BLOCK] = {0}; imp[0] = 1.0f;
    float acc[8 * BLOCK]; int total = 0;
    for (int b = 0; b < 8; b++) { fx_process(&S, b == 0 ? imp : (float[BLOCK]){0}, L, R, BLOCK); memcpy(acc + total, L, sizeof(L)); total += BLOCK; }
    int echoAt = -1; for (int i = 1; i < total; i++) if (acc[i] > 0.3f) { echoAt = i; break; }
    CHECK(echoAt > 0 && abs(echoAt - SR / 10) <= 3, "delay produces one echo 100 ms later");
    int later = 0; for (int i = echoAt + 10; i < total; i++) if (fabs(acc[i]) > 0.01f) later++;
    CHECK(later == 0, "no feedback -> no second echo");

    /* Everything on with the wildest presets stays finite and bounded. */
    fx_init(&S);
    for (int f = 0; f < FX_COUNT; f++) { fx_config_default(f, &c); fx_config_load_preset(f, &c, 4); c.enabled = 1; fx_apply(&S, f, &c); }
    float peak = 0; int finite = 1;
    for (int b = 0; b < 200; b++) {
        for (int i = 0; i < BLOCK; i++) in[i] = 0.6f * sinf(TWO_PI * 220 * (b * BLOCK + i) / SR) * (b < 20 ? 1 : 0);
        fx_process(&S, in, L, R, BLOCK);
        if (!all_finite(L, BLOCK) || !all_finite(R, BLOCK)) finite = 0;
        for (int i = 0; i < BLOCK; i++) { if (fabsf(L[i]) > peak) peak = fabsf(L[i]); }
    }
    CHECK(finite, "all five wildest presets stacked: output stays finite for 3 s");
    CHECK(peak < 4.0f, "stacked output stays bounded");
    float tail = 0; for (int i = 0; i < BLOCK; i++) if (fabsf(L[i]) > tail) tail = fabsf(L[i]);
    CHECK(tail < 0.05f, "tails decay after the input stops");

    /* Distortion at drive 0 and mix 1 is (nearly) unity. */
    fx_init(&S); fx_config_default(FX_DISTORTION, &c); c.enabled = 1; c.values[0] = 0; c.values[1] = 12000; c.values[2] = 0; c.values[3] = 1;
    fx_apply(&S, FX_DISTORTION, &c);
    for (int i = 0; i < BLOCK; i++) in[i] = 0.3f;
    fx_process(&S, in, L, R, BLOCK);
    /* tanh(x)/tanh(1) at x = 0.3 is 0.38: same mild curve as the mockup */
    CHECK_NEAR(L[BLOCK - 1], 0.38f, 0.03, "distortion with drive 0 matches the mockup curve");
    MT_END();
}
