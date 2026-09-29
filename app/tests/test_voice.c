#include "minitest.h"
#include "voice.h"
#include <string.h>

static Sample S;

static float peak_of(const float *a, int n) { float p = 0; for (int i = 0; i < n; i++) if (fabsf(a[i]) > p) p = fabsf(a[i]); return p; }

int main(void) {
    sample_make_builtin(&S);
    Voice v; float out[BLOCK];

    voice_note_on(&v, INST_SAMPLE, 60, &S, 1);
    for (int b = 0; b < 4; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }
    CHECK_NEAR(v.pos, 4 * BLOCK, 1.0, "C4 plays the sample at 1x");
    voice_note_on(&v, INST_SAMPLE, 72, &S, 2);
    for (int b = 0; b < 4; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }
    CHECK_NEAR(v.pos, 8 * BLOCK, 1.0, "C5 plays the sample at 2x");
    voice_bend(&v, 12.0f);
    double before = v.pos; memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK);
    CHECK_NEAR(v.pos - before, 4 * BLOCK, 1.0, "bend +12 doubles the rate again");
    voice_note_on(&v, INST_SAMPLE, 60, &S, 3);
    for (int b = 0; b < 80; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }   /* > 1 s: wraps */
    CHECK(v.pos >= S.loopStart && v.pos < S.length && v.active, "sample loops between loopStart and end");
    voice_note_off(&v);
    for (int b = 0; b < 10; b++) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); }
    CHECK(!v.active, "sample voice deactivates after release");

    const char *names[] = {"dream", "bells", "aurora", "supersaw", "wobble"};
    for (int inst = INST_DREAM; inst < INST_COUNT; inst++) {
        voice_note_on(&v, inst, 60, &S, 4);
        float peak = 0; int finite = 1;
        for (int b = 0; b < 64; b++) {            /* 1 s held */
            memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK);
            for (int i = 0; i < BLOCK; i++) if (!isfinite(out[i])) finite = 0;
            float p = peak_of(out, BLOCK); if (p > peak) peak = p;
        }
        char msg[64]; snprintf(msg, sizeof msg, "%s: finite, audible (peak %.2f) and below 1.5", names[inst - 1], peak);
        CHECK(finite && peak > 0.05f && peak < 1.5f, msg);
        voice_note_off(&v);
        int blocks = 0;
        while (v.active && blocks < 64 * 4) { memset(out, 0, sizeof(out)); voice_render(&v, out, BLOCK); blocks++; }
        snprintf(msg, sizeof msg, "%s: releases within 4 s (%d blocks)", names[inst - 1], blocks);
        CHECK(!v.active, msg);
    }
    MT_END();
}
