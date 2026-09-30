#include "minitest.h"
#include "fight.h"
#include "fishing.h"
#include "world.h"
#include "mesh.h"
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

/* A fake angler standing at sea level on Verdant above deep water, so every cast lands in the
   sea. player_update is never called; fishing only reads the fields set here. */
static void stand_on_sea(const world *w, player *p) {
    memset(p, 0, sizeof *p);
    p->dom = BODY_VERDANT;
    const body *b = &w->b[BODY_VERDANT];
    v3 up = v3_(0, 1, 0);
    for (int k = 0; k < 4000; k++) {
        float a = k * 0.37f, e = k * 0.11f;
        v3 d = v3_norm(v3_(cosf(a) * cosf(e), sinf(e), sinf(a) * cosf(e)));
        if (world_height_raw(w, BODY_VERDANT, d) < b->sea_level - 4.0f) { up = d; break; }
    }
    p->up = p->body_up = up;
    p->pos = v3_add(b->pos, v3_scale(up, world_sea_radius(w, BODY_VERDANT) + PLAYER_HALF_HEIGHT));
    p->facing = p->body_fwd = v3_any_perp(up);
    p->grounded = 1;
}

static fishing_input make_in(const player *p, int equip, int r_down, int r_pressed) {
    fishing_input in;
    memset(&in, 0, sizeof in);
    v3 up = p->up, fwd = p->body_fwd, right = v3_cross(up, fwd);
    in.hand = v3_add(v3_add(v3_add(p->pos, v3_scale(right, 0.4f)), v3_scale(up, 0.2f)), v3_scale(fwd, 0.7f));
    in.hand_fwd = fwd; in.hand_up = up;
    in.cast_dir = fwd;
    in.equip_pressed = equip; in.r_down = r_down; in.r_pressed = r_pressed;
    return in;
}
/* Holds (or not) R for secs. */
static void run_for(fishing *f, const world *w, const player *p, int r_down, float secs) {
    int n = (int)(secs / DT + 0.5f);
    for (int i = 0; i < n; i++) { fishing_input in = make_in(p, 0, r_down, 0); fishing_update(f, w, p, &in, DT); }
}
/* One frame with a button going down this frame. */
static void press(fishing *f, const world *w, const player *p, int equip, int r) {
    fishing_input in = make_in(p, equip, r, r);
    fishing_update(f, w, p, &in, DT);
}
/* Runs until the state changes or secs pass; returns 1 if it changed. */
static int run_until_leaves(fishing *f, const world *w, const player *p, int r_down, fishing_state s, float secs) {
    int n = (int)(secs / DT + 0.5f);
    for (int i = 0; i < n; i++) {
        fishing_input in = make_in(p, 0, r_down, 0);
        fishing_update(f, w, p, &in, DT);
        if (f->state != s) return 1;
    }
    return 0;
}
static float surface_dist(const player *p, v3 pos) { return v3_len(v3_tangent(v3_sub(pos, p->pos), p->up)); }

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

    world w; world_init(&w);
    player p; stand_on_sea(&w, &p);
    float sea_r = world_sea_radius(&w, BODY_VERDANT);
    CHECK(sea_r > 0, "Verdant has a sea");

    /* equip: swings up, then the bobber hangs under the tip */
    fishing f; fishing_init(&f);
    press(&f, &w, &p, 1, 0);
    CHECK(f.state == FISH_EQUIPPING, "d-pad down equips the rod");
    run_for(&f, &w, &p, 0, 0.45f);
    CHECK(f.state == FISH_HANGING, "after 0.4 s the bobber hangs");
    run_for(&f, &w, &p, 0, 1.0f);
    {
        rod_frame r; fishing_rod_frame(&f, &r);
        v3 tip = fishing_rod_point(&r, 1.0f);
        float below = v3_dot(v3_sub(tip, f.bobber), p.up);
        CHECK(below > 0.5f && below < 0.9f, "bobber hangs about 0.75 below the tip");
        CHECK(f.line_visible && f.bobber_visible, "line and bobber are shown while hanging");
    }

    /* full cast: hold R 2 s, release, lands on the sea 10-16 out */
    press(&f, &w, &p, 0, 1);
    CHECK(f.state == FISH_CHARGING, "R starts charging");
    run_for(&f, &w, &p, 1, 2.0f);
    CHECK_NEAR(f.charge, 1.0f, 1e-3f, "2 s of hold is a full charge");
    CHECK(run_until_leaves(&f, &w, &p, 0, FISH_CHARGING, 0.1f) && f.state == FISH_CASTING, "release casts");
    CHECK(run_until_leaves(&f, &w, &p, 0, FISH_CASTING, 1.0f) && f.state == FISH_FLIGHT, "the bobber leaves the rod");
    CHECK(run_until_leaves(&f, &w, &p, 0, FISH_FLIGHT, 4.0f) && f.state == FISH_WAITING, "the bobber lands in the sea");
    CHECK_NEAR(v3_len(v3_sub(f.bobber, w.b[BODY_VERDANT].pos)), sea_r, 0.06f, "the bobber floats at the sea radius");
    {
        float d = surface_dist(&p, f.bobber);
        printf("     (full cast landed %.1f out)\n", d);
        CHECK(d > 10.0f && d < 16.0f, "a full cast reaches 10-16 units");
    }

    /* reel home: nothing moves until the line is tight, then it comes back and hangs */
    {
        float d0 = surface_dist(&p, f.bobber);
        run_for(&f, &w, &p, 1, 0.3f);
        CHECK(fabsf(surface_dist(&p, f.bobber) - d0) < 0.05f, "the first 0.3 s of reeling only tightens the line");
        CHECK(run_until_leaves(&f, &w, &p, 1, FISH_WAITING, 6.0f) && f.state == FISH_TOWING, "reeling brings the bobber to the shore");
        CHECK(run_until_leaves(&f, &w, &p, 1, FISH_TOWING, 4.0f) && f.state == FISH_HANGING, "then up to the rod");
    }

    /* tap cast is short */
    run_for(&f, &w, &p, 0, 0.5f);
    press(&f, &w, &p, 0, 1);
    run_for(&f, &w, &p, 1, 0.05f);
    run_until_leaves(&f, &w, &p, 0, FISH_CHARGING, 0.1f);
    run_until_leaves(&f, &w, &p, 0, FISH_CASTING, 1.0f);
    CHECK(run_until_leaves(&f, &w, &p, 0, FISH_FLIGHT, 4.0f) && f.state == FISH_WAITING, "a tap cast also lands");
    {
        float d = surface_dist(&p, f.bobber);
        printf("     (tap cast landed %.1f out)\n", d);
        CHECK(d > 3.0f && d < 8.0f, "a tap cast reaches 3-8 units");
    }

    /* bite: the fish approaches, the window passes, the fish leaves */
    f.bite_timer = 0;
    run_for(&f, &w, &p, 0, DT * 2);
    CHECK(f.state == FISH_APPROACH && f.fish_visible, "the bite timer brings a visible fish");
    CHECK(run_until_leaves(&f, &w, &p, 0, FISH_APPROACH, 6.0f) && f.state == FISH_STRIKE, "the fish takes the float after the approach");
    CHECK(run_until_leaves(&f, &w, &p, 0, FISH_STRIKE, 4.0f) && f.state == FISH_RETREAT, "an unanswered strike ends in a retreat");
    CHECK(run_until_leaves(&f, &w, &p, 0, FISH_RETREAT, 3.0f) && f.state == FISH_WAITING, "then back to waiting");
    CHECK(!f.fish_visible, "the fish is gone");
    CHECK_NEAR(f.lost_bite_bonus, 1.3f, 1e-3f, "a lost bite speeds up the next one");

    /* hook and land a common with a pulsed reel */
    f.bite_timer = 0;
    run_for(&f, &w, &p, 0, DT * 2);
    f.tier = 0; f.weight_frac = 0;   /* force a small common so the pulse pattern cannot snap it */
    run_until_leaves(&f, &w, &p, 0, FISH_APPROACH, 6.0f);
    CHECK(f.state == FISH_STRIKE, "second bite");
    press(&f, &w, &p, 0, 1);
    CHECK(f.state == FISH_FIGHT, "R during the strike hooks the fish");
    {
        float t = 0;
        while (f.state == FISH_FIGHT && t < 60.0f) {
            fishing_input in = make_in(&p, 0, fmodf(t, 1.4f) < 0.8f, 0);
            fishing_update(&f, &w, &p, &in, DT);
            t += DT;
        }
        printf("     (fight took %.1f s)\n", t);
        CHECK(f.state == FISH_TOWING, "the pulsed reel lands the fish");
        CHECK(run_until_leaves(&f, &w, &p, 1, FISH_TOWING, 6.0f) && f.state == FISH_HANGING, "the fish is towed to the rod");
        CHECK(f.caught == 1, "the tally counts it");
        CHECK(f.banner_t > 0 && strncmp(f.banner, "CAUGHT!", 7) == 0, "the CAUGHT banner shows");
    }

    /* unequip mid-cast clears everything */
    run_for(&f, &w, &p, 0, 0.5f);
    press(&f, &w, &p, 0, 1);
    run_for(&f, &w, &p, 1, 1.0f);
    run_until_leaves(&f, &w, &p, 0, FISH_CHARGING, 0.1f);
    run_until_leaves(&f, &w, &p, 0, FISH_CASTING, 1.0f);
    CHECK(f.state == FISH_FLIGHT, "bobber in flight");
    press(&f, &w, &p, 1, 0);
    CHECK(f.state == FISH_UNEQUIPPING && !f.bobber_visible && !f.line_visible && !f.fish_visible, "d-pad down in flight puts the rod away at once");
    run_for(&f, &w, &p, 0, 0.45f);
    CHECK(f.state == FISH_OFF, "rod is off after 0.4 s");

    /* leaving the ground drops the rod immediately */
    press(&f, &w, &p, 1, 0);
    run_for(&f, &w, &p, 0, 0.6f);
    CHECK(f.state == FISH_HANGING, "rod out again");
    p.grounded = 0;
    run_for(&f, &w, &p, 0, DT);
    CHECK(f.state == FISH_OFF && !f.bobber_visible, "jumping puts the rod away");
    p.grounded = 1;

    /* fish mesh */
    {
        mesh fm = mesh_fish();
        CHECK(fm.count == 30, "fish mesh is 30 vertices");
        float zmin = 1e9f, zmax = -1e9f;
        for (int i = 0; i < fm.count; i++) { if (fm.v[i].pos[2] < zmin) zmin = fm.v[i].pos[2]; if (fm.v[i].pos[2] > zmax) zmax = fm.v[i].pos[2]; }
        CHECK(zmax > 0.25f && zmin < -0.35f, "fish runs from the tail to the head along z");
        mesh_free(&fm);
    }
    MT_END();
}
