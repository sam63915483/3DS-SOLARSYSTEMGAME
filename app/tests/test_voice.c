#include "minitest.h"
#include "voice.h"
#include <string.h>

static Sample S;
static float raw[MAX_REC_SAMPLES];

static float peak_of(const float *a, int n) { float p = 0; for (int i = 0; i < n; i++) if (fabsf(a[i]) > p) p = fabsf(a[i]); return p; }

int main(void) {
    int n = sample_builtin_raw(raw);
    const SampleMode *user = &MODE_PRESETS[0].settings;
    sample_process(&S, raw, n, user, 1);
    Voice v; float out[BLOCK];

    voice_note_on(&v, INST_SAMPLE, 60, &S, user, 1);
    for (int b = 0; b < 4; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }
    CHECK_NEAR(v.pos, 4 * BLOCK, 1.0, "C4 plays the sample at 1x");
    voice_note_on(&v, INST_SAMPLE, 72, &S, user, 2);
    for (int b = 0; b < 4; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }
    CHECK_NEAR(v.pos, 8 * BLOCK, 1.0, "C5 plays the sample at 2x");
    voice_bend(&v, 12.0f);
    double before = v.pos; memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK);
    CHECK_NEAR(v.pos - before, 4 * BLOCK, 1.0, "bend +12 doubles the rate again");
    voice_note_on(&v, INST_SAMPLE, 60, &S, user, 3);
    for (int b = 0; b < 80; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }   /* > 1 s: wraps */
    CHECK(v.pos >= S.loopStart && v.pos < S.length && v.active, "loop mode: sample loops between loopStart and end");
    voice_note_off(&v);
    for (int b = 0; b < 10; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }
    CHECK(!v.active, "sample voice deactivates after release");

    /* SK-1 mode: stored at 9380 Hz, one-shot. C4 must still play at original pitch (1 s long). */
    const SampleMode *sk1 = &MODE_PRESETS[1].settings;
    sample_process(&S, raw, n, sk1, 1);
    voice_note_on(&v, INST_SAMPLE, 60, &S, sk1, 4);
    int blocks = 0;
    while (v.active && blocks < 200) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); blocks++; }
    CHECK(!v.active && abs(blocks - SR / BLOCK) <= 2, "one-shot: ends by itself after ~1 s at C4 even though stored at 9.4 kHz");
    voice_note_on(&v, INST_SAMPLE, 72, &S, sk1, 5);
    blocks = 0;
    while (v.active && blocks < 200) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); blocks++; }
    CHECK(abs(blocks - SR / BLOCK / 2) <= 2, "one-shot at C5 lasts half as long");

    /* DRONE mode: 800 ms attack means the first 100 ms are quiet. */
    const SampleMode *drone = &MODE_PRESETS[3].settings;
    sample_process(&S, raw, n, drone, 1);
    voice_note_on(&v, INST_SAMPLE, 60, &S, drone, 6);
    memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK);
    CHECK(v.amp < 0.05f, "slow attack: envelope still low after the first block");
    for (int b = 0; b < 60; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }
    CHECK(v.amp > 0.95f, "slow attack reaches full level after ~1 s");

    /* DUSTY envelope (400 ms decay to 60%) held on a looping sample settles at the sustain level. */
    SampleMode dusty = MODE_PRESETS[2].settings; dusty.loop = 1;
    sample_process(&S, raw, n, &dusty, 1);
    voice_note_on(&v, INST_SAMPLE, 60, &S, &dusty, 7);
    for (int b = 0; b < 128; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }   /* 2 s */
    CHECK(v.active && fabsf(v.amp - 0.6f) < 0.02f, "decay/sustain: level settles at 60% while held");

    sample_process(&S, raw, n, user, 1);
    const char *names[] = {"dream", "bells", "aurora", "supersaw", "wobble"};
    for (int inst = INST_DREAM; inst < INST_COUNT; inst++) {
        voice_note_on(&v, inst, 60, &S, NULL, 8);
        float peak = 0; int finite = 1;
        for (int b = 0; b < 64; b++) {
            memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK);
            for (int i = 0; i < BLOCK; i++) if (!isfinite(out[i])) finite = 0;
            float p = peak_of(out, BLOCK); if (p > peak) peak = p;
        }
        char msg[64]; snprintf(msg, sizeof msg, "%s: finite, audible (peak %.2f) and below 1.5", names[inst - 1], peak);
        CHECK(finite && peak > 0.05f && peak < 1.5f, msg);
        voice_note_off(&v);
        blocks = 0;
        while (v.active && blocks < 64 * 4) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); blocks++; }
        snprintf(msg, sizeof msg, "%s: releases within 4 s (%d blocks)", names[inst - 1], blocks);
        CHECK(!v.active, msg);
    }
    MT_END();
}
