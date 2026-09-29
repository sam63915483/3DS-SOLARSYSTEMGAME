#include "minitest.h"
#include "sample.h"
#include <stdlib.h>
#include <string.h>

static Sample S;   /* too big for the stack */
static float raw[MAX_REC_SAMPLES];
#define WIN (SR / 100)

static float peak_of(const float *a, int n) { float p = 0; for (int i = 0; i < n; i++) if (fabsf(a[i]) > p) p = fabsf(a[i]); return p; }

int main(void) {
    char txt[24];
    /* Modes: 4 presets, only USER editable, all values valid. */
    CHECK(MODE_PRESET_COUNT == 4 && !MODE_PRESETS[0].locked && MODE_PRESETS[1].locked && MODE_PRESETS[2].locked && MODE_PRESETS[3].locked, "four mode presets, only USER editable");
    int valid = 1;
    for (int p = 0; p < MODE_PRESET_COUNT; p++) for (int f = 0; f < MODE_FIELD_COUNT; f++) {
        float v = mode_get(&MODE_PRESETS[p].settings, f);
        const ModeField *fd = &MODE_FIELDS[f];
        int okv = 0;
        if (fd->nChoices) { for (int k = 0; k < fd->nChoices; k++) if (fabsf(fd->choices[k] - v) < 1e-3f) okv = 1; }
        else okv = v >= fd->min && v <= fd->max;
        if (!okv) { valid = 0; printf("  %s.%s = %g invalid\n", MODE_PRESETS[p].name, fd->label, v); }
    }
    CHECK(valid, "all mode preset values valid");
    CHECK(MODE_PRESETS[1].settings.rate == 9380 && MODE_PRESETS[1].settings.bits == 8 && !MODE_PRESETS[1].settings.loop, "SK-1 preset: 9.4 kHz 8-bit one-shot");
    CHECK(mode_step(MF_RATE, 32728, 1) == 32728 && mode_step(MF_RATE, 32728, -1) == 26040 && mode_step(MF_RATE, 8000, -1) == 8000, "choice fields step and clamp");
    CHECK_NEAR(mode_step(MF_ATTACK, 0, 1), 25, 1e-4, "slider fields step 2.5%");
    CHECK(mode_step(MF_ATTACK, 1000, 1) == 1000, "slider fields clamp");
    mode_format(MF_RATE, 9380, txt); CHECK(strcmp(txt, "9.4 kHz") == 0, "rate formatting");
    mode_format(MF_LOOP, 0, txt); CHECK(strcmp(txt, "OFF (one-shot)") == 0, "loop formatting");
    SampleMode um = MODE_PRESETS[0].settings; mode_set(&um, MF_BITS, 8); CHECK(um.bits == 8, "mode_set");

    /* Processing: SK-1 mode turns 1 s at SR into 9380 samples on an 8-bit grid, no loop point. */
    for (int i = 0; i < SR; i++) raw[i] = 0.7f * sinf(i / 20.0f);
    sample_process(&S, raw, SR, &MODE_PRESETS[1].settings, 0);
    CHECK(abs(S.length - 9380) <= 1 && S.loopStart == 0 && S.rate == 9380 && !S.loop, "SK-1 mode: 9380 samples, one-shot");
    int onGrid = 1;
    for (int i = 0; i < S.length; i++) if (fabsf(S.data[i] * 128 - roundf(S.data[i] * 128)) > 1e-4f) onGrid = 0;
    CHECK(onGrid, "8-bit: every stored value sits on a 1/128 step");
    sample_process(&S, raw, SR, &MODE_PRESETS[0].settings, 0);
    CHECK(S.length == SR && S.loopStart == SR * 50 / 1000 && S.loop, "USER mode: native rate, 50 ms loop point");

    /* Trimming on a 10 ms envelope. */
    int n = SR * 5 / 2;
    memset(raw, 0, n * sizeof(float));
    for (int i = SR; i < SR * 3 / 2; i++) raw[i] = 0.5f * sinf(i / 10.0f);
    int start, end;
    sample_trim_range(raw, n, &start, &end);
    CHECK(abs(start - (SR - SR / 100)) <= WIN, "trim start is ~10 ms before the tone");
    CHECK(abs(end - (SR * 3 / 2 + SR * 60 / 1000)) <= WIN, "trim end is ~60 ms after the tone");
    for (int i = 0; i < SR; i++) raw[i] = 0.005f;
    sample_trim_range(raw, SR, &start, &end);
    CHECK(start == 0 && end == SR, "all-quiet recording is kept whole");

    /* A faint click at 0.5 s must not become the start when a loud sound follows at 2 s. */
    int m3 = SR * 3;
    memset(raw, 0, m3 * sizeof(float));
    for (int i = SR / 2; i < SR / 2 + SR / 50; i++) raw[i] = 0.03f * sinf(i / 3.0f);
    for (int i = SR * 2; i < SR * 2 + SR / 2; i++) raw[i] = 0.5f * sinf(i / 7.0f);
    sample_trim_range(raw, m3, &start, &end);
    CHECK(abs(start - (SR * 2 - SR / 100)) <= WIN, "faint click before the loud sound is ignored");

    /* Trim + normalise a soft sound. */
    static float out[MAX_REC_SAMPLES];
    memset(raw, 0, SR * 2 * sizeof(float));
    for (int i = SR / 2; i < SR; i++) raw[i] = 0.15f * sinf(i / 9.0f);
    int trimmedMs, len = sample_trim_normalize(raw, SR * 2, out, &trimmedMs);
    CHECK(trimmedMs >= 480 && trimmedMs <= 500, "soft sound: trimmed about 490 ms");
    CHECK_NEAR(peak_of(out, len), 0.9f, 0.02, "soft sound normalised to 0.9 peak");

    /* Loop crossfade continuity: a sine that does not end on a period boundary. */
    int m = SR / 2 + 137;
    for (int i = 0; i < m; i++) raw[i] = 0.8f * sinf(6.2831853f * 220.0f * i / SR);
    sample_process(&S, raw, m, &MODE_PRESETS[0].settings, 0);
    double seamJump = fabs(S.data[m - 1] - S.data[S.loopStart]);
    double step = fabs(raw[1] - raw[0]);
    CHECK(seamJump < step * 3, "loop seam is continuous");
    CHECK(S.data[0] == 0.0f, "start fade applied");

    len = sample_builtin_raw(raw);
    CHECK(len == SR && peak_of(raw, len) > 0.3f && peak_of(raw, len) <= 1.0f, "built-in tone is one second at a sane level");
    MT_END();
}
