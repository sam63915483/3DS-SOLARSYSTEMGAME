#include "minitest.h"
#include "mixer.h"
#include <stdlib.h>
#include <string.h>

static Mixer M;   /* ~4 MB: static */
static float rec[MAX_REC_SAMPLES];

static int peak_abs(const short *o, int n) { int p = 0; for (int i = 0; i < 2 * n; i++) { int a = o[i] < 0 ? -o[i] : o[i]; if (a > p) p = a; } return p; }

int main(void) {
    short out[2 * BLOCK];
    mixer_init(&M);
    mixer_render(&M, out, BLOCK);
    CHECK(peak_abs(out, BLOCK) == 0 && M.scopeRms == 0 && M.scopeMidi == -1, "silence when nothing is held");
    CHECK(M.sample.isBuiltIn && M.sample.rate == 32728 && M.sample.loop, "starts with the built-in tone in USER mode");

    mixer_note_on(&M, 60);
    for (int b = 0; b < 8; b++) mixer_render(&M, out, BLOCK);
    CHECK(peak_abs(out, BLOCK) > 2000 && M.scopeMidi == 60, "built-in tone plays at C4");
    CHECK(abs(out[0] - out[1]) < 2, "mono source -> identical L and R");

    mixer_note_on(&M, 67);
    CHECK(M.held >= 0 && M.voices[M.held].midi == 67, "retrigger moves to the new note");
    int active = 0; for (int i = 0; i < MAX_VOICES; i++) if (M.voices[i].active) active++;
    CHECK(active == 2, "old voice is still fading (cut), new voice active");
    for (int b = 0; b < 6; b++) mixer_render(&M, out, BLOCK);   /* ~94 ms */
    active = 0; for (int i = 0; i < MAX_VOICES; i++) if (M.voices[i].active) active++;
    CHECK(active == 1, "cut voice freed within 100 ms");

    mixer_set_pad(&M, 0, 1.0f);
    CHECK_NEAR(M.voices[M.held].bend, 12.0f, 1e-6, "circle pad up = +12 semitones on the held voice");
    mixer_set_pad(&M, -1.0f, 0);
    for (int b = 0; b < 4; b++) mixer_render(&M, out, BLOCK);
    int dark = peak_abs(out, BLOCK);
    mixer_set_pad(&M, 0, 0);
    for (int b = 0; b < 4; b++) mixer_render(&M, out, BLOCK);
    int flat = peak_abs(out, BLOCK);
    CHECK(dark < flat, "full-left pad (low-pass at 250 Hz) is quieter than flat");

    mixer_set_muted(&M, 1);
    for (int b = 0; b < 20; b++) mixer_render(&M, out, BLOCK);
    CHECK(peak_abs(out, BLOCK) == 0 && M.held == -1, "muted for recording: silent, note released");
    mixer_set_muted(&M, 0);

    /* Modes: switching to SK-1 re-processes the same sound; one-shot notes end on their own. */
    mixer_set_mode(&M, &MODE_PRESETS[1].settings);
    CHECK(M.sample.rate == 9380 && !M.sample.loop && M.sample.isBuiltIn, "SK-1 mode re-processes the built-in tone at 9.4 kHz one-shot");
    mixer_note_on(&M, 60);
    int blocks = 0;
    while (M.held >= 0 && blocks < 200) { mixer_render(&M, out, BLOCK); blocks++; }
    CHECK(M.held == -1 && abs(blocks - SR / BLOCK) <= 3, "one-shot note frees the held slot after ~1 s");
    mixer_set_mode(&M, &MODE_PRESETS[0].settings);

    /* A recording: 0.5 s quiet, 0.3 s tone -> trimmed, normalised, becomes the instrument. */
    memset(rec, 0, sizeof(rec));
    for (int i = SR / 2; i < SR / 2 + SR * 3 / 10; i++) rec[i] = 0.2f * sinf(i / 5.0f);
    mixer_set_instrument(&M, INST_BELLS);
    mixer_set_recording(&M, rec, SR);
    CHECK(!M.sample.isBuiltIn && M.instrument == INST_SAMPLE, "new recording becomes YOUR SAMPLE");
    CHECK(M.sample.trimmedMs >= 480 && M.sample.trimmedMs <= 500, "recording trimmed ~490 ms");
    CHECK(M.sample.length > SR * 3 / 10 && M.sample.length < SR * 3 / 10 + SR / 10, "sample length ~0.3 s plus tail");
    float pk = 0; for (int i = 0; i < M.sample.length; i++) if (fabsf(M.sample.data[i]) > pk) pk = fabsf(M.sample.data[i]);
    CHECK_NEAR(pk, 0.9f, 0.02, "recording normalised");

    for (int inst = INST_DREAM; inst < INST_COUNT; inst++) {
        mixer_set_instrument(&M, inst);
        mixer_note_on(&M, 60);
        for (int b = 0; b < 30; b++) mixer_render(&M, out, BLOCK);
        int p = peak_abs(out, BLOCK);
        char msg[64]; snprintf(msg, sizeof msg, "%s plays through the mixer (peak %d)", INST_NAMES[inst], p);
        CHECK(p > 1000 && p < 32767, msg);
        mixer_note_off(&M);
    }

    /* Every effect on with its wildest preset: still finite, no NaN resets. */
    FxConfig c;
    for (int f = 0; f < FX_COUNT; f++) { fx_config_default(f, &c); fx_config_load_preset(f, &c, 4); c.enabled = 1; mixer_apply_fx(&M, f, &c); }
    mixer_set_instrument(&M, INST_SUPERSAW);
    mixer_note_on(&M, 48);
    for (int b = 0; b < 200; b++) mixer_render(&M, out, BLOCK);
    CHECK(M.nanResets == 0 && peak_abs(out, BLOCK) > 0, "all effects stacked for 3 s: no NaN resets, still sounding");
    MT_END();
}
