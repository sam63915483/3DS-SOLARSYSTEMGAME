#include "minitest.h"
#include "mixer.h"
#include <stdlib.h>
#include <string.h>

static Mixer M;   /* ~2 MB: static */

static int peak_abs(const short *o, int n) { int p = 0; for (int i = 0; i < 2 * n; i++) { int a = o[i] < 0 ? -o[i] : o[i]; if (a > p) p = a; } return p; }

int main(void) {
    short out[2 * BLOCK];
    mixer_init(&M);
    mixer_render(&M, out, BLOCK);
    CHECK(peak_abs(out, BLOCK) == 0 && M.scopeRms == 0 && M.scopeMidi == -1, "silence when nothing is held");

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
