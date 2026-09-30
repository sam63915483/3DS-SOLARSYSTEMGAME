#include "minitest.h"
#include "fight.h"
#include <string.h>

#define DT (1.0f / 60.0f)

/* Runs the fight with the given hold pattern until it ends or max_s passes; returns the time or -1. */
static float fight_until_done(fight *f, int (*hold)(float), float max_s) {
    for (float t = 0; t < max_s; t += DT) {
        fight_step(f, DT, hold(t));
        if (f->outcome != FIGHT_ON) return t;
    }
    return -1;
}
static int always(float t) { (void)t; return 1; }
static int never(float t) { (void)t; return 0; }
static int pulse(float t) { return fmodf(t, 1.4f) < 0.8f; }

int main(void) {
    /* fight: a rare fish snaps the line under a steady hold in a few seconds */
    {
        fight f; fight_start(&f, 2, 1.0f, 10.0f, 0.0f, 1u);
        float t = fight_until_done(&f, always, 20.0f);
        CHECK(f.outcome == FIGHT_SNAPPED, "steady hold on a rare snaps the line");
        CHECK(t > 1.0f && t < 4.0f, "the snap comes between 1 and 4 s (about 1.3 if you hold into the first run)");
    }
    /* fight: pulsing the reel lands a common without ever snapping */
    {
        fight f; fight_start(&f, 0, 0.0f, 10.0f, 0.0f, 2u);
        float t = fight_until_done(&f, pulse, 40.0f);
        CHECK(f.outcome == FIGHT_LANDED, "hold 0.8 / release 0.6 lands a common");
        CHECK(t > 0 && t < 40.0f, "the common lands within 40 s");
    }
    /* fight: letting go bleeds tension quickly */
    {
        fight f; fight_start(&f, 1, 0.5f, 8.0f, 1.0f, 3u);
        f.tension = 60;
        for (int i = 0; i < 42; i++) fight_step(&f, DT, 0);   /* 0.7 s */
        CHECK_NEAR(f.tension, 0.0f, 1e-3f, "60 tension drains to 0 within 0.7 s of releasing");
    }
    /* fight: holding on a slack line builds no tension until it comes tight */
    {
        fight f; fight_start(&f, 0, 0.5f, 8.0f, 0.0f, 4u);
        for (int i = 0; i < 24; i++) fight_step(&f, DT, 1);   /* 0.4 s: taut about 0.89 */
        CHECK_NEAR(f.tension, 0.0f, 1e-3f, "no tension while the line is still coming tight");
        CHECK(f.taut > 0.8f && f.taut < 0.985f, "line is most of the way tight after 0.4 s");
    }
    /* fight: never reeling lets the fish slip off */
    {
        fight f; fight_start(&f, 0, 0.5f, 8.0f, 0.0f, 5u);
        float t = fight_until_done(&f, never, 30.0f);
        CHECK(f.outcome == FIGHT_SLIPPED, "a fish on a slack line slips off");
        CHECK(t > 3.0f, "it takes at least 3 s of slack");
    }
    MT_END();
}
