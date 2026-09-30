# Orbit Fishing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A fishing rod in the Orbit 3DS demo: equip with D-pad down, hold R to cast, reel with R, hook a bite with R, fight a fish on a tension meter, land it for a CAUGHT banner and tally — a port of the Sound of Space (Unity) mechanic per `docs/superpowers/specs/2026-09-30-orbit-fishing-design.md`.

**Architecture:** Two portable C sim modules (`fight.c` tension sim, `fishing.c` state machine + bobber + fish + rod angles) with no 3DS headers, tested on the host by `tests/run_tests.sh`. `render.c` draws rod/line/bobber/fish/splash, `fishhud.c` draws the top-screen tension bar and banner, `hud.c` shows status/controls/tally, `main.c` wires the buttons.

**Tech Stack:** C (gnu99), libctru + citro3d + citro2d, devkitARM `make`; host gcc via MSYS2 for tests (`bash tests/run_tests.sh`).

Working directory for every command: `C:\3ds\orbit` (git repo root is `C:\3ds`). Run host tests from Git Bash: `bash tests/run_tests.sh`. Build the 3DS binary with MSYS2: `C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; cd /c/3ds/orbit && make"`.

---

## File map

| File | Responsibility |
|---|---|
| `source/fight.h`, `source/fight.c` (new) | Tension fight sim: runs, stamina, line tautness, tension, distance, outcome. Also the shared `frandf` LCG. |
| `source/fishing.h`, `source/fishing.c` (new) | Rod state machine, charge/cast, bobber flight/float/reel, bite, fish approach/thrash/retreat, rod bend/haul/recoil, banner, tally. Owns a `fight`. |
| `source/fishhud.h`, `source/fishhud.c` (new) | Top-screen citro2d overlay: tension bar, HOOK! prompt, CAUGHT banner. |
| `source/mesh.h`, `source/mesh.c` | `mesh_fish()`. |
| `source/render.h`, `source/render.c` | `draw_fishing_solid` (rod, bobber, fish), `draw_fishing_soft` (line, splash), raised right arm. `render_scene` takes the `fishing`. |
| `source/player.h`, `source/player.c` | `player_input.face`: turn the model to face the cast while the rod is out. |
| `source/hud.h`, `source/hud.c` | FISHING status, rod controls, tally. |
| `source/main.c` | D-pad down / R input, hand frame, update + draw calls. |
| `tests/test_fishing.c` (new), `tests/run_tests.sh` | Host tests for `fight` and `fishing`. |

---

### Task 1: `fight` — the tension sim

**Files:**
- Create: `source/fight.h`, `source/fight.c`
- Create: `tests/test_fishing.c`
- Modify: `tests/run_tests.sh`

- [ ] **Step 1: Add the new sources to the host test build**

In `tests/run_tests.sh` change the gcc line to:

```bash
  gcc -std=gnu99 -Wall -Wextra -Werror -O1 -Isource -Itests "$t" source/world.c source/player.c source/camera.c source/mesh.c source/noise.c source/fight.c source/fishing.c -lm -o "build/tests/$name.exe"
```

- [ ] **Step 2: Write the failing fight tests**

Create `tests/test_fishing.c`:

```c
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
        CHECK(t > 1.5f && t < 4.0f, "the snap comes between 1.5 and 4 s");
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
```

- [ ] **Step 3: Run the tests to see them fail**

Run: `bash tests/run_tests.sh`
Expected: gcc error `fight.h: No such file or directory` (and `fishing.c` missing).

- [ ] **Step 4: Write `source/fight.h`**

```c
/* The fish fight: a tension sim ported from Sound of Space's FishFightSim. Plain C, no 3DS
   headers, so the host tests can drive it. Tension only builds while the angler holds the reel
   AND the line is tight; letting go bleeds it fast. The fish runs now and then, taking line and
   pulling 1.6x harder, and tires as it runs and as it is reeled. */
#ifndef FIGHT_H
#define FIGHT_H
#include <stdint.h>

typedef enum { FIGHT_ON, FIGHT_LANDED, FIGHT_SNAPPED, FIGHT_SLIPPED } fight_outcome;

typedef struct {
    int tier;                 /* 0 common, 1 uncommon, 2 rare */
    float weight_frac;        /* 0..1 within the tier's weight range */
    float tension;            /* 0..100; 100 snaps the line */
    float distance;           /* fish to angler along the surface */
    float start_distance;
    float stamina, stamina_start;   /* seconds of fight left in the fish */
    float taut;               /* 0 slack .. 1 tight */
    int running;              /* the fish is taking line */
    int run_started;          /* set on the frame a run begins; the caller clears it */
    float run_t, run_len, next_run;
    float slack_s, idle_s;    /* seconds of total slack; seconds since the angler last held */
    float pull;               /* current pull: base, up to 1.6x in a run; drives the rod bend */
    fight_outcome outcome;
    uint32_t rng;
} fight;

/* Uniform random in lo..hi from a tiny LCG; fishing.c shares it. */
static inline float frandf(uint32_t *s, float lo, float hi) {
    *s = *s * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((*s >> 8) / 16777216.0f);
}

void fight_start(fight *f, int tier, float weight_frac, float distance, float taut0, uint32_t seed);
/* One frame. holding: the reel button is down. */
void fight_step(fight *f, float dt, int holding);
static inline int fight_tight(const fight *f) { return f->taut >= 0.985f; }
static inline float fight_vigour(const fight *f) { return f->stamina_start > 0 ? f->stamina / f->stamina_start : 0; }

#endif
```

- [ ] **Step 5: Write `source/fight.c`**

```c
#include "fight.h"
#include <string.h>

/* per tier: common, uncommon, rare */
static const float BASE_PULL[3] = { 1.0f, 1.4f, 1.8f };
static const float RUN_SPEED[3] = { 3.2f, 3.9f, 5.5f };
static const float STAM_LO[3] = { 5.0f, 7.8f, 15.0f }, STAM_HI[3] = { 7.0f, 11.8f, 23.5f };
static const float RESIST_MAX[3] = { 0.70f, 0.85f, 0.93f };
static const float FIRST_LO[3] = { 0.4f, 0.5f, 0.5f }, FIRST_HI[3] = { 0.8f, 1.0f, 0.9f };
static const float GAP_LO[3] = { 1.5f, 1.6f, 1.25f }, GAP_HI[3] = { 2.6f, 2.7f, 2.1f };

#define TENSION_RATE   80.0f   /* per unit pull per second while tight and holding */
#define BLEED_RATE     95.0f   /* per second while released */
#define REEL_RATE      13.0f   /* distance per second at zero resistance */
#define TAUT_ON       (1.0f / 0.45f)
#define TAUT_ON_RUN   (1.0f / (0.45f * 0.35f))
#define TAUT_OFF      (1.0f / 1.2f)
#define SLIP_SECONDS   3.0f

static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float toward(float x, float target, float step) {
    if (x < target) { x += step; return x > target ? target : x; }
    x -= step; return x < target ? target : x;
}

void fight_start(fight *f, int tier, float weight_frac, float distance, float taut0, uint32_t seed) {
    memset(f, 0, sizeof *f);
    if (tier < 0) tier = 0;
    if (tier > 2) tier = 2;
    f->tier = tier;
    f->weight_frac = clamp01(weight_frac);
    f->rng = seed;
    f->distance = f->start_distance = distance;
    f->stamina = f->stamina_start = STAM_LO[tier] + (STAM_HI[tier] - STAM_LO[tier]) * f->weight_frac;
    f->taut = clamp01(taut0);
    f->next_run = frandf(&f->rng, FIRST_LO[tier], FIRST_HI[tier]);
    f->pull = BASE_PULL[tier];
    f->outcome = FIGHT_ON;
}

void fight_step(fight *f, float dt, int holding) {
    if (f->outcome != FIGHT_ON) return;
    const int t = f->tier;
    int spent = f->stamina <= 0;
    float vig = fight_vigour(f);

    /* 1. runs: a rested fish bolts every couple of seconds; a spent one never does */
    f->run_started = 0;
    if (f->running) {
        f->run_t += dt;
        if (f->run_t >= f->run_len) { f->running = 0; f->next_run = frandf(&f->rng, GAP_LO[t], GAP_HI[t]); }
    } else if (!spent) {
        f->next_run -= dt;
        if (f->next_run <= 0) {
            f->running = 1; f->run_started = 1; f->run_t = 0;
            f->run_len = frandf(&f->rng, 1.0f, 2.0f) * (0.15f + 0.85f * vig);
        }
    }
    float ramp = 0;
    if (f->running) { float x = clamp01(f->run_t / 0.2f); ramp = x * x * (3 - 2 * x); }
    f->pull = BASE_PULL[t] * (1.0f + 0.6f * ramp);

    /* 2. a running fish takes line and tires */
    if (f->running) {
        f->distance += RUN_SPEED[t] * ramp * dt;
        if (f->distance > f->start_distance * 1.6f) f->distance = f->start_distance * 1.6f;
        f->stamina -= dt;
    }

    /* 3. the line: pulled tight by either party, droops slowly when both let go; a live fish
       keeps it most of the way tight for a second after the angler lets go */
    if (holding) f->idle_s = 0; else f->idle_s += dt;
    int any_pull = holding || f->running;
    float rate, target;
    if (any_pull) {
        rate = (holding && f->running) ? TAUT_ON_RUN : TAUT_ON;
        target = 1;
    } else {
        rate = TAUT_OFF;
        float hold_fade = 1.0f - clamp01(f->idle_s / 1.0f);
        target = spent ? 0 : vig * hold_fade * 0.85f;
    }
    f->taut = toward(f->taut, target, rate * dt);

    /* 4. force: only a tight line under a held reel loads the rod and moves the fish */
    if (holding && fight_tight(f)) {
        float scale = (0.38f + 0.22f * ramp) * (0.3f + 0.7f * vig);
        f->tension += TENSION_RATE * f->pull * scale * dt;
        f->stamina -= dt;
        float resist = RESIST_MAX[t] * (0.8f + 0.2f * f->weight_frac);
        if (spent) resist *= 0.5f;
        f->distance -= REEL_RATE * (1.0f - resist) * dt;
    } else if (!holding) {
        f->tension -= BLEED_RATE * dt;
        if (f->tension < 0) f->tension = 0;
        if (f->tension <= 0 && f->taut <= 0.01f) f->slack_s += dt;
    }
    if (f->stamina < 0) f->stamina = 0;

    /* 5. outcome */
    if (f->tension >= 100) f->outcome = FIGHT_SNAPPED;
    else if (f->distance <= 1.2f) f->outcome = FIGHT_LANDED;
    else if (f->slack_s >= SLIP_SECONDS) f->outcome = FIGHT_SLIPPED;
}
```

- [ ] **Step 6: Create an empty `source/fishing.c` so the test build links**

Create `source/fishing.c` containing only:

```c
#include "fight.h"
```

- [ ] **Step 7: Run the tests**

Run: `bash tests/run_tests.sh`
Expected: `== test_fishing` then every line `ok`, `N passed, 0 failed`; `test_sim` still passes.

- [ ] **Step 8: Commit**

```bash
cd /c/3ds && git add orbit/source/fight.h orbit/source/fight.c orbit/source/fishing.c orbit/tests/test_fishing.c orbit/tests/run_tests.sh
git commit -m "Orbit: fish fight tension sim (port of FishFightSim) with host tests

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: `player_input.face` — the model turns to face the cast

**Files:**
- Modify: `source/player.h:29-40`, `source/player.c:163-166`

- [ ] **Step 1: Add the field**

In `source/player.h`, inside `player_input` after `float roll;` add:

```c
    v3 face;           /* on foot: turn the model to face this world direction (zero: face the walk) */
```

- [ ] **Step 2: Use it**

In `source/player.c` replace

```c
    /* facing follows the input, stays tangent */
    if (mlen > 0.1f) p->facing = v3_lerp(p->facing, v3_norm(move), ease(10.0f, dt));
```

with

```c
    /* facing follows the walk, or the requested direction when standing still, stays tangent */
    {
        v3 face = mlen > 0.1f ? move : in->face;
        if (v3_len(face) > 0.1f) p->facing = v3_lerp(p->facing, v3_norm(face), ease(10.0f, dt));
    }
```

- [ ] **Step 3: Run the tests**

Run: `bash tests/run_tests.sh`
Expected: all pass (test_sim zero-initialises `player_input`, so `face` is zero and nothing changes).

- [ ] **Step 4: Commit**

```bash
cd /c/3ds && git add orbit/source/player.h orbit/source/player.c
git commit -m "Orbit: player_input.face turns the standing model toward a requested direction

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: `fishing` — state machine, bobber, fish, rod angles

**Files:**
- Create: `source/fishing.h`
- Replace: `source/fishing.c`
- Modify: `tests/test_fishing.c`

- [ ] **Step 1: Write the failing state-machine tests**

Append to `tests/test_fishing.c` (add `#include "fishing.h"` and `#include "world.h"` at the top; insert the helpers before `main` and the new blocks before `MT_END()`):

```c
#include "fishing.h"
#include "world.h"

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
```

and inside `main`, before `MT_END();`:

```c
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
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `bash tests/run_tests.sh`
Expected: gcc error `fishing.h: No such file or directory`.

- [ ] **Step 3: Write `source/fishing.h`**

```c
/* Fishing rod: equip with the d-pad, hold R to cast, hold R to reel, tap R to hook a bite, then
   fight the fish on the tension sim in fight.h. Plain C, no 3DS headers. Positions are world
   space; velocities are relative to the player's dominant body, like the player's. */
#ifndef FISHING_H
#define FISHING_H
#include "world.h"
#include "player.h"
#include "fight.h"

typedef enum {
    FISH_OFF,        /* rod not equipped */
    FISH_EQUIPPING,  /* 0.4 s swing up */
    FISH_HANGING,    /* bobber dangles under the tip, ready */
    FISH_CHARGING,   /* R held */
    FISH_CASTING,    /* wind-up + snap; the bobber leaves part way */
    FISH_FLIGHT,     /* bobber in the air */
    FISH_GROUNDED,   /* bobber lying on land */
    FISH_WAITING,    /* bobber on water, bite timer runs */
    FISH_APPROACH,   /* fish spirals up (cosmetic) */
    FISH_STRIKE,     /* hook window */
    FISH_FIGHT,      /* fight sim runs */
    FISH_TOWING,     /* bobber (and any fish) dragged to the rod tip */
    FISH_RETREAT,    /* fish flees after a miss / snap / slip, then WAITING */
    FISH_UNEQUIPPING /* 0.4 s swing down */
} fishing_state;

typedef struct {
    fishing_state state;
    float t;                  /* seconds in the current state */
    float time;               /* seconds since init, for wobbles */
    float charge;             /* 0..1 */
    float draw_t;             /* rod wind-up 0..1 */
    float cast_draw, cast_from, cast_back;   /* cast timeline: seconds to finish the draw, pose angles */
    v3 hand, hand_fwd, hand_up;   /* where the rod is held, copied from the input */
    v3 bobber, bobber_vel, bobber_up;   /* bobber_up: away from the dominant body at the bobber */
    v3 anchor;                /* point on the sea (or ground) the floating bobber is tied to */
    float bob_off;            /* bobber height above the anchor: 0 seated, negative submerged */
    v3 wander, wander_target; float wander_t;
    float tug_t, tug_phase;
    v3 glue_off;              /* CHARGING: the bobber's offset from the tip, shrinking to zero */
    int bobber_visible, line_visible;
    float line_taut;          /* 0..1 for the line's sag */
    float leash;              /* hanging: length of line below the tip */
    float retrieve_taut;      /* 0..1 outside a fight */
    int reeling;              /* R held and the retrieve is active */
    int r_released_since_cast;
    float bite_timer;         /* seconds until the fish arrives */
    float lost_bite_bonus;    /* 1, or 1.3 after a lost bite this cast */
    int tier; float weight_frac;
    float strike_window;
    float spawn_depth;
    v3 fish_pos, fish_fwd, fish_up; float fish_phase, fish_scale; int fish_visible;
    v3 retreat_from, retreat_dir;
    int with_fish;            /* TOWING: a landed fish is on the line */
    int hookset_pending;
    fight fight;
    float rod_pose, rod_haul, rod_bend, rod_recoil;  /* degrees; see fishing_rod_frame */
    float tw_from, tw_to, tw_t, tw_len;              /* rod_pose tween */
    float recoil_t;                                  /* -1 idle, else seconds since the snap */
    float bar_shake;          /* HUD run tell, decays 3.5/s */
    float banner_t;           /* CAUGHT banner seconds left */
    char banner[32];
    int caught;               /* tally */
    v3 splash_pos; float splash_t;
    uint32_t rng;
} fishing;

typedef struct {
    int equip_pressed;        /* d-pad down went down this frame */
    int r_down, r_pressed;    /* R held / went down this frame */
    v3 cast_dir;              /* unit vector the camera looks along */
    v3 hand, hand_fwd, hand_up;   /* the rod's butt and the plane it swings in (unit vectors) */
} fishing_input;

/* The rod: butt position and unbent direction after pose and haul; bent about bend_axis
   through the butt by bend_rad * s^2 at fraction s along the rod. */
typedef struct { v3 butt, dir, bend_axis; float bend_rad; } rod_frame;

void fishing_init(fishing *f);
/* Once per frame after player_update and camera_update. */
void fishing_update(fishing *f, const world *w, const player *p, const fishing_input *in, float dt);
void fishing_rod_frame(const fishing *f, rod_frame *r);
/* Point on the bent rod at fraction s (0 butt .. 1 tip). */
v3 fishing_rod_point(const rod_frame *r, float s);
static inline int fishing_active(const fishing *f) { return f->state != FISH_OFF; }
const char *fishing_tier_name(int tier);

#endif
```

- [ ] **Step 4: Write `source/fishing.c`**

```c
#include "fishing.h"
#include <string.h>
#include <stdio.h>

#define ROD_LEN        1.6f
#define ROD_REST_ELEV 35.0f    /* degrees above the hand's forward */
#define EQUIP_TIME     0.4f
#define HANG_LEASH     0.75f
#define LEASH_GROW     1.4f
#define CHARGE_TIME    2.0f
#define DRAW_TIME      0.15f
#define WIND_TIME      0.22f
#define CAST_SPEED_MIN  8.0f   /* tap: about 6 units on Verdant from a 2.2-high tip */
#define CAST_SPEED_MAX 20.0f   /* full hold: about 14 */
#define REEL_SPEED     6.6f
#define REEL_STEP_MAX 17.0f
#define TAUT_ON       (1.0f / 0.45f)
#define TAUT_OFF      (1.0f / 1.2f)
#define ARRIVE_DIST    1.6f
#define HANG_DIST      1.1f
#define APPROACH_TIME  5.38f
#define RETREAT_TIME   2.4f
#define BANNER_TIME    2.5f
#define BOBBER_R       0.12f
#define DEG ((float)M_PI / 180.0f)

static const float STRIKE_WINDOW[3] = { 3.0f, 2.0f, 1.5f };
static const float FISH_SCALE[3] = { 0.6f, 1.0f, 1.5f };
static const float WEIGHT_LO[3] = { 0.4f, 1.5f, 4.0f }, WEIGHT_HI[3] = { 1.5f, 4.0f, 12.0f };
static const char *TIER_NAME[3] = { "COMMON", "UNCOMMON", "RARE" };

const char *fishing_tier_name(int tier) { return TIER_NAME[tier < 0 ? 0 : (tier > 2 ? 2 : tier)]; }

static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float smoothstep(float x) { x = clamp01(x); return x * x * (3 - 2 * x); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float toward(float x, float target, float step) {
    if (x < target) { x += step; return x > target ? target : x; }
    x -= step; return x < target ? target : x;
}
static v3 v3_toward(v3 a, v3 b, float step) {
    v3 d = v3_sub(b, a); float l = v3_len(d);
    return l <= step ? b : v3_add(a, v3_scale(d, step / l));
}

/* ---- rod ---- */

void fishing_rod_frame(const fishing *f, rod_frame *r) {
    /* pose and recoil swing the rod forward (Unity's positive X), haul pulls it back */
    float e = (ROD_REST_ELEV - f->rod_pose - f->rod_recoil + f->rod_haul) * DEG;
    r->butt = f->hand;
    r->dir = v3_norm(v3_add(v3_scale(f->hand_fwd, cosf(e)), v3_scale(f->hand_up, sinf(e))));
    r->bend_rad = f->rod_bend * DEG;
    v3 axis = v3_cross(r->dir, v3_sub(f->bobber, f->hand));   /* bends toward the bobber */
    r->bend_axis = v3_len(axis) > 1e-4f ? v3_norm(axis) : v3_any_perp(r->dir);
}

v3 fishing_rod_point(const rod_frame *r, float s) {
    return v3_add(r->butt, v3_rotate(v3_scale(r->dir, ROD_LEN * s), r->bend_axis, r->bend_rad * s * s));
}

static v3 tip_of(const fishing *f) { rod_frame r; fishing_rod_frame(f, &r); return fishing_rod_point(&r, 1.0f); }

static void tween(fishing *f, float to, float len) { f->tw_from = f->rod_pose; f->tw_to = to; f->tw_t = 0; f->tw_len = len; }

/* ---- geometry around the dominant body ---- */

static v3 up_at(const world *w, const player *p, v3 pos) { return v3_norm(v3_sub(pos, w->b[p->dom].pos)); }
/* distance along the ground between the player and pos, in the player's tangent plane */
static float surf_dist(const player *p, v3 pos) { return v3_len(v3_tangent(v3_sub(pos, p->pos), p->up)); }
static v3 toward_player(const player *p, v3 pos) {
    v3 t = v3_tangent(v3_sub(p->pos, pos), p->up);
    return v3_len(t) > 1e-4f ? v3_norm(t) : v3_any_perp(p->up);
}
static v3 on_sea(const world *w, const player *p, v3 pos) {
    return v3_add(w->b[p->dom].pos, v3_scale(up_at(w, p, pos), world_sea_radius(w, p->dom) - 0.01f));
}

/* ---- state helpers ---- */

static void enter(fishing *f, fishing_state s) { f->state = s; f->t = 0; }

static void clear_tackle(fishing *f) {
    f->bobber_visible = f->line_visible = f->fish_visible = 0;
    f->reeling = 0; f->retrieve_taut = 0; f->line_taut = 0;
    f->charge = 0; f->draw_t = 0;
    f->hookset_pending = 0; f->with_fish = 0;
    f->fight.outcome = FIGHT_LANDED;
    f->rod_bend = f->rod_haul = 0;
}

static void new_bite_timer(fishing *f) { f->bite_timer = frandf(&f->rng, 5.0f, 14.0f); }

static int roll_tier(fishing *f, float d) {
    float k = clamp01((d - 4.0f) / 10.0f);   /* long casts skew rarer */
    float wc = 70 - 25 * k, wu = 25 + 15 * k, wr = 5 + 10 * k;
    float x = frandf(&f->rng, 0, wc + wu + wr);
    return x < wc ? 0 : (x < wc + wu ? 1 : 2);
}

static void start_waiting(fishing *f) {
    enter(f, FISH_WAITING);
    f->fish_visible = 0;
    new_bite_timer(f);
}

static void start_retreat(fishing *f, const player *p) {
    f->retreat_from = f->fish_pos;
    f->retreat_dir = v3_scale(toward_player(p, f->bobber), -1.0f);
    f->wander = f->wander_target = v3_(0, 0, 0);
    enter(f, FISH_RETREAT);
}

static void start_towing(fishing *f, int with_fish) {
    f->with_fish = with_fish;
    f->reeling = 0;
    f->retrieve_taut = 1;
    enter(f, FISH_TOWING);
}

/* ---- fish ---- */

/* Fish hanging off the float: head at the bobber, body pointing away from the angler, yawing. */
static void fish_on_float(fishing *f, const player *p, float vig, int running, float dt) {
    float amp = lerpf(4, 30, vig) * (running ? 1.5f : 1.0f) * DEG;
    float rate = lerpf(2, 10, vig) * (running ? 2.4f : 1.0f);
    f->fish_phase += rate * dt;
    v3 up = f->bobber_up, away = v3_scale(toward_player(p, f->bobber), -1.0f);
    v3 fwd = v3_scale(v3_rotate(away, up, sinf(f->fish_phase) * amp), -1.0f);   /* head towards the float */
    float pitch = (12.0f + (running ? 10.0f : 0.0f)) * DEG;
    fwd = v3_norm(v3_add(v3_scale(fwd, cosf(pitch)), v3_scale(up, sinf(pitch))));
    float half = 0.25f * f->fish_scale;
    f->fish_fwd = fwd;
    f->fish_up = v3_norm(v3_tangent(up, fwd));
    f->fish_pos = v3_add(v3_sub(f->bobber, v3_scale(fwd, half)), v3_scale(up, 0.02f + 0.1f * half));
    f->fish_visible = 1;
}

static void face_along(fishing *f, v3 pos, v3 up) {
    v3 d = v3_sub(pos, f->fish_pos);
    if (v3_len(d) > 1e-4f) {
        d = v3_norm(d);
        float u = v3_dot(d, up);
        if (fabsf(u) > 0.7071f) {   /* pitch clamped to 45 degrees */
            v3 t = v3_tangent(d, up);
            t = v3_len(t) > 1e-4f ? v3_norm(t) : v3_any_perp(up);
            d = v3_norm(v3_add(v3_scale(t, 0.7071f), v3_scale(up, u > 0 ? 0.7071f : -0.7071f)));
        }
        f->fish_fwd = d;
    }
    f->fish_pos = pos;
    f->fish_up = v3_norm(v3_tangent(up, f->fish_fwd));
    f->fish_visible = 1;
}

/* Helix up from the depths on the far side of the float, level off, pounce. */
static void fish_approach(fishing *f, const player *p, float t) {
    v3 up = f->bobber_up, away = v3_scale(toward_player(p, f->anchor), -1.0f), side = v3_cross(up, away);
    float r, d, ang;
    if (t < 4.5f) {
        float k = t / 4.5f;
        ang = 2.0f * (float)M_PI * 1.4f * k;
        r = lerpf(2.8f, 0.65f, k);
        d = lerpf(f->spawn_depth, 0.5f, smoothstep((k - 0.25f) / 0.75f));
    } else if (t < 5.1f) {
        float k = (t - 4.5f) / 0.6f;
        ang = 2.0f * (float)M_PI * 1.4f;
        r = lerpf(0.65f, 0.5f, k); d = lerpf(0.5f, 0.42f, k);
    } else {
        float k = clamp01((t - 5.1f) / 0.28f); k *= k;
        ang = 2.0f * (float)M_PI * 1.4f;
        r = lerpf(0.5f, 0.02f, k); d = lerpf(0.42f, -0.04f, k);
    }
    v3 pos = v3_add(v3_add(f->anchor, v3_scale(away, r * cosf(ang))), v3_add(v3_scale(side, r * sinf(ang)), v3_scale(up, -d)));
    face_along(f, pos, up);
}

static void fish_retreat(fishing *f, float t) {
    float k = clamp01(t / RETREAT_TIME);
    v3 pos = v3_add(v3_add(f->retreat_from, v3_scale(f->retreat_dir, 5.5f * k)), v3_scale(f->bobber_up, -2.5f * k * k));
    face_along(f, pos, f->bobber_up);
    if (t >= RETREAT_TIME) f->fish_visible = 0;
}

/* ---- bobber on the water ---- */

static void update_wander(fishing *f, const player *p, float vig, float dt) {
    f->wander_t -= dt;
    if (f->wander_t <= 0) {
        float v = vig < 0.05f ? 0.05f : vig;
        f->wander_t = frandf(&f->rng, 0.6f, 1.8f) / v;
        v3 up = f->bobber_up, away = v3_scale(toward_player(p, f->anchor), -1.0f), side = v3_cross(up, away);
        f->wander_target = v3_add(v3_scale(side, frandf(&f->rng, -1.0f, 1.0f)), v3_scale(away, frandf(&f->rng, -0.7f, 0.7f)));
    }
    f->wander = v3_toward(f->wander, f->wander_target, 0.5f * dt);
}

/* Slides the anchor along the sea toward the player by up to step. */
static void reel_anchor(fishing *f, const world *w, const player *p, float step) {
    float d = surf_dist(p, f->anchor);
    if (step > d) step = d;
    f->anchor = on_sea(w, p, v3_add(f->anchor, v3_scale(toward_player(p, f->anchor), step)));
}

/* True when the reeled bobber has reached the angler or run up onto the shore. */
static int reel_arrived(const fishing *f, const world *w, const player *p) {
    if (surf_dist(p, f->anchor) <= ARRIVE_DIST) return 1;
    v3 ahead = v3_add(f->anchor, v3_scale(toward_player(p, f->anchor), 0.35f));
    v3 d = v3_sub(ahead, w->b[p->dom].pos);
    return world_surface(w, p->dom, v3_norm(d)) > world_sea_radius(w, p->dom);
}

static void update_retrieve_taut(fishing *f, const fishing_input *in, float dt) {
    f->reeling = in->r_down && f->r_released_since_cast;
    f->retrieve_taut = toward(f->retrieve_taut, f->reeling ? 1.0f : 0.0f, (f->reeling ? TAUT_ON : TAUT_OFF) * dt);
}

/* ---- main update ---- */

void fishing_init(fishing *f) {
    memset(f, 0, sizeof *f);
    f->state = FISH_OFF;
    f->rng = 0x9E3779B9u;
    f->recoil_t = -1;
    f->lost_bite_bonus = 1;
    f->fight.outcome = FIGHT_LANDED;
    f->splash_t = 10;
}

void fishing_update(fishing *f, const world *w, const player *p, const fishing_input *in, float dt) {
    const body *dom = &w->b[p->dom];
    f->hand = in->hand; f->hand_fwd = in->hand_fwd; f->hand_up = in->hand_up;
    f->time += dt;
    if (f->banner_t > 0) f->banner_t -= dt;
    f->bar_shake -= 3.5f * dt; if (f->bar_shake < 0) f->bar_shake = 0;
    f->splash_t += dt;

    int on_ground = p->grounded && !p->swimming;
    if (f->state != FISH_OFF && !on_ground) {   /* jumped, fell in, blasted off: rod away at once */
        clear_tackle(f);
        f->state = FISH_OFF;
        return;
    }
    if (f->state == FISH_OFF) {
        if (!(in->equip_pressed && on_ground)) return;
        clear_tackle(f);
        f->rod_pose = -120; tween(f, 0, EQUIP_TIME);
        f->bobber = tip_of(f); f->bobber_vel = v3_(0, 0, 0);
        f->bobber_visible = 1;
        enter(f, FISH_EQUIPPING);
    } else if (in->equip_pressed && f->state != FISH_UNEQUIPPING) {
        clear_tackle(f);
        tween(f, 180, EQUIP_TIME);
        enter(f, FISH_UNEQUIPPING);
    }

    /* everything rides along with the dominant body */
    v3 ride = v3_sub(dom->pos, dom->prev);
    f->bobber = v3_add(f->bobber, ride);
    f->anchor = v3_add(f->anchor, ride);
    f->fish_pos = v3_add(f->fish_pos, ride);
    f->retreat_from = v3_add(f->retreat_from, ride);
    f->splash_pos = v3_add(f->splash_pos, ride);
    f->bobber_up = up_at(w, p, f->bobber);
    if (!in->r_down) f->r_released_since_cast = 1;

    f->t += dt;
    if (f->tw_len > 0) {
        f->tw_t += dt;
        f->rod_pose = lerpf(f->tw_from, f->tw_to, smoothstep(f->tw_t / f->tw_len));
        if (f->tw_t >= f->tw_len) f->tw_len = 0;
    }
    if (f->recoil_t >= 0) {   /* snap: whip forward, then settle */
        f->recoil_t += dt;
        f->rod_recoil = f->recoil_t < 0.06f ? 42.0f * f->recoil_t / 0.06f : 42.0f * (1.0f - clamp01((f->recoil_t - 0.06f) / 0.45f));
        if (f->recoil_t > 0.51f) { f->rod_recoil = 0; f->recoil_t = -1; }
    }

    v3 tip = tip_of(f);
    float taut_for_line = f->retrieve_taut;
    int running = 0; float vig = 1;

    switch (f->state) {
    case FISH_OFF: break;

    case FISH_EQUIPPING:
        f->bobber = tip;
        if (f->t >= EQUIP_TIME) { f->leash = 0.02f; f->bobber_vel = v3_(0, 0, 0); enter(f, FISH_HANGING); }
        break;

    case FISH_HANGING: {
        /* the leash pays out to its resting length; the bobber swings on a stiff damped spring
           toward a point one leash below the tip and can never be further than the leash */
        f->leash = f->leash > HANG_LEASH ? toward(f->leash, HANG_LEASH, REEL_SPEED * dt) : toward(f->leash, HANG_LEASH, LEASH_GROW * dt);
        v3 target = v3_sub(tip, v3_scale(f->bobber_up, f->leash));
        v3 acc = v3_sub(v3_scale(v3_sub(target, f->bobber), 45.0f), v3_scale(f->bobber_vel, 7.0f));
        f->bobber_vel = v3_add(f->bobber_vel, v3_scale(acc, dt));
        float sp = v3_len(f->bobber_vel);
        if (sp > 20) f->bobber_vel = v3_scale(f->bobber_vel, 20.0f / sp);
        f->bobber = v3_add(f->bobber, v3_scale(f->bobber_vel, dt));
        v3 d = v3_sub(f->bobber, tip); float l = v3_len(d);
        if (l > f->leash && l > 1e-5f) {
            v3 n = v3_scale(d, 1.0f / l);
            f->bobber = v3_add(tip, v3_scale(n, f->leash));
            float out = v3_dot(f->bobber_vel, n);
            if (out > 0) f->bobber_vel = v3_sub(f->bobber_vel, v3_scale(n, out));
        }
        f->line_visible = 1;
        taut_for_line = 0.6f;
        if (in->r_pressed) {
            f->glue_off = v3_sub(f->bobber, tip);
            f->charge = 0; f->draw_t = 0; f->tw_len = 0;
            enter(f, FISH_CHARGING);
        }
        break;
    }

    case FISH_CHARGING:
        f->charge = clamp01(f->t / CHARGE_TIME);
        f->draw_t = clamp01(f->draw_t + dt / DRAW_TIME);
        f->rod_pose = -(50.0f * f->draw_t + 18.0f * f->charge);
        f->bobber = v3_add(tip, v3_scale(f->glue_off, 1.0f - smoothstep(f->t / WIND_TIME)));
        f->line_visible = 0;
        if (!in->r_down) {
            f->cast_draw = DRAW_TIME * (1.0f - f->draw_t);
            f->cast_from = f->rod_pose;
            f->cast_back = -(50.0f + 18.0f * f->charge);
            enter(f, FISH_CASTING);
        }
        break;

    case FISH_CASTING: {
        /* finish the draw, hold, snap forward (bobber released at 70 percent), settle */
        float t0 = f->cast_draw, t1 = t0 + 0.02f, t2 = t1 + 0.1f;
        f->bobber = tip;
        f->line_visible = 0;
        if (f->t < t0) f->rod_pose = lerpf(f->cast_from, f->cast_back, f->t / t0);
        else if (f->t < t1) f->rod_pose = f->cast_back;
        else {
            float k = clamp01((f->t - t1) / (t2 - t1));
            f->rod_pose = lerpf(f->cast_back, 5.0f, smoothstep(k));
            if (k >= 0.7f) {
                float speed = sqrtf(lerpf(CAST_SPEED_MIN * CAST_SPEED_MIN, CAST_SPEED_MAX * CAST_SPEED_MAX, f->charge));
                f->bobber = tip_of(f);
                f->bobber_vel = v3_add(p->vel, v3_scale(v3_norm(in->cast_dir), speed));
                f->r_released_since_cast = 0;
                f->retrieve_taut = 0;
                f->lost_bite_bonus = 1;
                tween(f, 0, 0.13f);
                enter(f, FISH_FLIGHT);
            }
        }
        break;
    }

    case FISH_FLIGHT: {
        v3 g = v3_sub(world_gravity_at(w, f->bobber), world_frame_accel(w, p->dom));
        f->bobber_vel = v3_add(f->bobber_vel, v3_scale(g, dt));
        f->bobber = v3_add(f->bobber, v3_scale(f->bobber_vel, dt));
        f->line_visible = 1;
        taut_for_line = 0;
        v3 d = v3_sub(f->bobber, dom->pos);
        float dist = v3_len(d);
        v3 dir = v3_scale(d, 1.0f / dist);
        float sea_r = world_sea_radius(w, p->dom);
        if (sea_r > 0 && dist < sea_r) {
            f->splash_pos = on_sea(w, p, f->bobber); f->splash_t = 0;
            f->anchor = f->splash_pos;
            f->bob_off = 0;
            f->wander = f->wander_target = v3_(0, 0, 0);
            f->bobber_vel = v3_(0, 0, 0);
            start_waiting(f);
        } else if (dist < world_surface(w, p->dom, dir) + BOBBER_R) {
            f->bobber = v3_add(dom->pos, v3_scale(dir, world_surface(w, p->dom, dir) + BOBBER_R));
            f->bobber_vel = v3_(0, 0, 0);
            enter(f, FISH_GROUNDED);
        }
        break;
    }

    case FISH_GROUNDED:
        f->line_visible = 1;
        update_retrieve_taut(f, in, dt);
        if (f->retrieve_taut >= 0.985f) start_towing(f, 0);
        break;

    case FISH_WAITING:
    case FISH_APPROACH:
    case FISH_STRIKE:
    case FISH_FIGHT:
    case FISH_RETREAT: {
        f->line_visible = 1;
        float target_off = (sinf(f->time * 1.2f) - 1.0f) * 0.015f;   /* idle: a dip of up to 3 cm */
        float extra_v = 0, lat = 0, along = 0;
        int fast_follow = 0;

        if (f->state == FISH_WAITING || f->state == FISH_APPROACH || f->state == FISH_RETREAT) {
            update_retrieve_taut(f, in, dt);
            if (f->reeling && f->retrieve_taut >= 0.985f) {
                float step = REEL_SPEED * dt;
                if (step > REEL_STEP_MAX * dt) step = REEL_STEP_MAX * dt;
                reel_anchor(f, w, p, step);
                if (reel_arrived(f, w, p)) { f->fish_visible = 0; start_towing(f, 0); break; }
            }
            if (f->state == FISH_WAITING) {
                f->bite_timer -= dt * (f->reeling ? 1.2f : 1.0f) * f->lost_bite_bonus;
                if (f->bite_timer <= 0) {
                    float d = surf_dist(p, f->anchor);
                    f->tier = roll_tier(f, d);
                    f->weight_frac = frandf(&f->rng, 0, 1);
                    f->fish_scale = FISH_SCALE[f->tier];
                    float bed = world_sea_radius(w, p->dom) - world_surface(w, p->dom, up_at(w, p, f->anchor));
                    f->spawn_depth = bed - 0.4f;
                    if (f->spawn_depth < 1.2f) f->spawn_depth = 1.2f;
                    if (f->spawn_depth > 7.0f) f->spawn_depth = 7.0f;
                    /* start the fish at its spawn point so its first heading is sane */
                    v3 away = v3_scale(toward_player(p, f->anchor), -1.0f);
                    f->fish_pos = v3_add(v3_add(f->anchor, v3_scale(away, 2.8f)), v3_scale(f->bobber_up, -f->spawn_depth));
                    f->fish_fwd = v3_scale(away, -1.0f);
                    f->fish_up = v3_norm(v3_tangent(f->bobber_up, f->fish_fwd));
                    f->fish_visible = 1;
                    enter(f, FISH_APPROACH);
                }
            } else if (f->state == FISH_APPROACH) {
                fish_approach(f, p, f->t);
                if (f->t >= APPROACH_TIME) {
                    f->strike_window = STRIKE_WINDOW[f->tier];
                    f->wander = f->wander_target = v3_(0, 0, 0); f->wander_t = 0;
                    f->fish_phase = 0;
                    enter(f, FISH_STRIKE);
                }
            } else {
                fish_retreat(f, f->t);
                if (f->t >= RETREAT_TIME) start_waiting(f);
            }
        } else if (f->state == FISH_STRIKE) {
            fast_follow = 1;
            target_off = -0.12f;
            extra_v = (sinf(f->time * 16.0f) * 0.5f - 0.35f) * 0.02f;   /* quick shiver, biased down */
            update_wander(f, p, 1.0f, dt);
            fish_on_float(f, p, 1.0f, 0, dt);
            if (in->r_pressed || (in->r_down && f->reeling)) {
                float d = surf_dist(p, f->anchor);
                fight_start(&f->fight, f->tier, f->weight_frac, d > 2.2f ? d : 2.2f, f->retrieve_taut, f->rng);
                f->rng ^= f->fight.rng * 2654435761u;
                f->hookset_pending = 1;
                f->reeling = 0; f->retrieve_taut = 0;
                f->tug_t = 0; f->tug_phase = 0;
                enter(f, FISH_FIGHT);
            } else if (f->t >= f->strike_window) {
                f->lost_bite_bonus = 1.3f;
                start_retreat(f, p);
            }
        } else {   /* FISH_FIGHT */
            fast_follow = 1;
            fight *g = &f->fight;
            g->distance = surf_dist(p, f->anchor);
            fight_step(g, dt, in->r_down);
            if (g->run_started) f->bar_shake = 1;
            running = g->running; vig = fight_vigour(g);
            {
                float delta = surf_dist(p, f->anchor) - g->distance;   /* positive: reel it in */
                float cap = REEL_STEP_MAX * dt;
                if (delta > cap) delta = cap;
                if (delta < -cap) delta = -cap;
                if (delta > 0) reel_anchor(f, w, p, delta);
                else if (delta < 0) f->anchor = on_sea(w, p, v3_sub(f->anchor, v3_scale(toward_player(p, f->anchor), -delta)));
            }
            taut_for_line = g->taut;
            target_off = -0.28f;
            /* tugs and jitter */
            f->tug_t -= dt;
            if (f->tug_t <= 0) { f->tug_phase = 1; f->tug_t = frandf(&f->rng, 0.35f, 1.1f) / (vig < 0.05f ? 0.05f : vig); }
            f->tug_phase = toward(f->tug_phase, 0, 5.0f * dt);
            float k = f->tug_phase * f->tug_phase;
            extra_v = -0.09f * k * lerpf(0.35f, 1.0f, vig);
            float amp = 0.035f * lerpf(0.2f, 1.0f, vig) * (running ? 1.8f : 1.0f);
            float tt = f->time * 2.0f * (float)M_PI;
            lat = sinf(12.0f * f->tug_phase) * 0.036f * k + amp * 0.5f * (sinf(tt * 13.7f) + sinf(tt * 27.1f));
            along = amp * 0.5f * (sinf(tt * 9.3f) + sinf(tt * 19.7f));
            update_wander(f, p, vig, dt);
            fish_on_float(f, p, vig, running, dt);
            if (f->hookset_pending && (g->taut >= 0.65f || f->t >= 1.5f)) { f->rod_haul = 25; f->hookset_pending = 0; }
            if (g->outcome == FIGHT_LANDED) { start_towing(f, 1); break; }
            if (g->outcome == FIGHT_SNAPPED) { f->recoil_t = 0; f->hookset_pending = 0; start_retreat(f, p); break; }
            if (g->outcome == FIGHT_SLIPPED) { f->lost_bite_bonus = 1.3f; f->hookset_pending = 0; start_retreat(f, p); break; }
        }

        /* place the bobber: seated on the anchor, dipped or submerged, plus wander and jitter */
        if (fast_follow) f->bob_off += (target_off - f->bob_off) * (1.0f - expf(-22.0f * dt));
        else f->bob_off = toward(f->bob_off, target_off, 0.6f * dt);
        if (f->state != FISH_STRIKE && f->state != FISH_FIGHT) f->wander = v3_toward(f->wander, v3_(0, 0, 0), 0.5f * dt);
        v3 away = v3_scale(toward_player(p, f->anchor), -1.0f), side = v3_cross(f->bobber_up, away);
        f->bobber = v3_add(v3_add(f->anchor, v3_scale(f->bobber_up, f->bob_off + extra_v)),
                           v3_add(f->wander, v3_add(v3_scale(side, lat), v3_scale(away, along))));
        break;
    }

    case FISH_TOWING: {
        f->line_visible = 1;
        taut_for_line = 1;
        float d = v3_len(v3_sub(tip, f->bobber));
        f->bobber = v3_toward(f->bobber, tip, REEL_SPEED * dt);
        if (f->with_fish) fish_on_float(f, p, 0.3f, 0, dt);
        if (d < HANG_DIST) {
            f->leash = d;
            f->bobber_vel = v3_(0, 0, 0);
            if (f->with_fish) {
                f->caught++;
                float kg = lerpf(WEIGHT_LO[f->tier], WEIGHT_HI[f->tier], f->weight_frac);
                snprintf(f->banner, sizeof f->banner, "CAUGHT! %s %.1f KG", TIER_NAME[f->tier], kg);
                f->banner_t = BANNER_TIME;
                f->fish_visible = 0;
                f->with_fish = 0;
            }
            f->retrieve_taut = 0;
            enter(f, FISH_HANGING);
        }
        break;
    }

    case FISH_UNEQUIPPING:
        if (f->t >= EQUIP_TIME) { f->state = FISH_OFF; f->tw_len = 0; }
        break;
    }

    /* rod bend and haul follow the line: nothing happens until it is tight */
    {
        float load = 0, haul_target = 0;
        if (f->state == FISH_FIGHT) {
            const fight *g = &f->fight;
            load = g->taut * (0.18f + 0.82f * g->tension / 100.0f);
            if (g->stamina <= 0) load *= 0.5f;
            if (in->r_down) haul_target = 18.0f + (g->running ? 7.0f : 0.0f);
        } else if (f->reeling) {
            load = 0.16f * f->retrieve_taut;
            haul_target = 7.2f;
        }
        haul_target *= clamp01((taut_for_line - 0.65f) / 0.35f);
        float curve = load < 0.45f ? load / 0.45f * 0.5f : 0.5f + 0.5f * powf((load - 0.45f) / 0.55f, 1.5f);
        float bend_target = 38.0f * curve;
        f->rod_bend += (bend_target - f->rod_bend) * ease(bend_target > f->rod_bend ? 8.0f : 16.0f, dt);
        f->rod_haul += (haul_target - f->rod_haul) * ease(haul_target > f->rod_haul ? 4.0f : 8.0f, dt);
        f->line_taut = taut_for_line;
    }
}
```

- [ ] **Step 5: Run the tests**

Run: `bash tests/run_tests.sh`
Expected: every `ok`, `N passed, 0 failed`. If a cast-distance check fails, print the distance and adjust `CAST_SPEED_MIN/MAX` (the ranges in the test are the target feel: tap 3–8, full 10–16).

- [ ] **Step 6: Commit**

```bash
cd /c/3ds && git add orbit/source/fishing.h orbit/source/fishing.c orbit/tests/test_fishing.c
git commit -m "Orbit: fishing state machine: equip, charge and cast, float, reel, bite, hook, fight, land

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: fish mesh

**Files:**
- Modify: `source/mesh.h:46-48`, `source/mesh.c` (after `mesh_box`)

- [ ] **Step 1: Declare it**

In `source/mesh.h` after the `mesh_astronaut` declaration add:

```c
/* A small fish in model space: +z is the head, +y up, about 0.5 long, origin mid-body. */
mesh mesh_fish(void);
```

- [ ] **Step 2: Implement it**

In `source/mesh.c` after `mesh_box` add:

```c
mesh mesh_fish(void) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(24 + 6, sizeof(vertex));
    emit_octa(&m, v3_(0, 0, 0.02f), AX, AY, AZ, 0.07f, 0.10f, 0.26f, 0x6E9A6EFF);   /* body */
    /* tail fin: one vertical triangle, both faces */
    v3 root = v3_(0, 0, -0.22f), tt = v3_(0, 0.12f, -0.40f), tb = v3_(0, -0.12f, -0.40f);
    emit_tri(&m, root, tt, tb, v3_(0.2f, 0, -0.3f), 0x5A8058FF);
    emit_tri(&m, root, tb, tt, v3_(-0.2f, 0, -0.3f), 0x5A8058FF);
    return m;
}
```

- [ ] **Step 3: Add a mesh test**

In `tests/test_fishing.c` before `MT_END();` add (`mesh.h` include at the top):

```c
    {
        mesh fm = mesh_fish();
        CHECK(fm.count == 30, "fish mesh is 30 vertices");
        float zmin = 1e9f, zmax = -1e9f;
        for (int i = 0; i < fm.count; i++) { if (fm.v[i].pos[2] < zmin) zmin = fm.v[i].pos[2]; if (fm.v[i].pos[2] > zmax) zmax = fm.v[i].pos[2]; }
        CHECK(zmax > 0.25f && zmin < -0.35f, "fish runs from the tail to the head along z");
        mesh_free(&fm);
    }
```

- [ ] **Step 4: Run the tests**

Run: `bash tests/run_tests.sh`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
cd /c/3ds && git add orbit/source/mesh.h orbit/source/mesh.c orbit/tests/test_fishing.c
git commit -m "Orbit: low-poly fish mesh

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: render the rod, bobber, fish, line and splash

**Files:**
- Modify: `source/render.h`, `source/render.c`

- [ ] **Step 1: Change the scene signature**

In `source/render.h` add `#include "fishing.h"` and change the declaration to:

```c
void render_scene(const world *w, const player *p, const camera *c, const fishing *f, float time);
```

- [ ] **Step 2: Add the meshes and buffers**

In `source/render.c`, after the `static gpu_mesh astro_body, ...` line add:

```c
static gpu_mesh rod_seg, bobber_top, bobber_bot, fish_mesh;
#define LINE_SEGS 12
#define LINE_VERTS (LINE_SEGS * 6)
#define SPLASH_VERTS (8 * 6)
static vertex *line_vbo, *splash_vbo;   /* rebuilt every frame */
#define ROD_ARM_SWING (-1.3f)   /* right arm raised forward to hold the rod; flip the sign if it points back */
```

In `render_init` after `stars = upload(...)` add:

```c
    rod_seg = upload(mesh_box(1, 1, 1, 0x6B4A2AFF));       /* unit box, scaled per segment */
    bobber_top = upload(mesh_sphere(0.12f, 6, 8, 0xE03030FF));
    bobber_bot = upload(mesh_sphere(0.11f, 6, 8, 0xF0F0F0FF));
    fish_mesh = upload(mesh_fish());
    line_vbo = linearAlloc(LINE_VERTS * sizeof(vertex));
    splash_vbo = linearAlloc(SPLASH_VERTS * sizeof(vertex));
```

In `render_fini` add:

```c
    gpu_free(&rod_seg); gpu_free(&bobber_top); gpu_free(&bobber_bot); gpu_free(&fish_mesh);
    linearFree(line_vbo); linearFree(splash_vbo);
```

- [ ] **Step 3: Raise the arm while fishing**

Change `draw_player`'s signature to `static void draw_player(const player *p, const camera *c, int fishing, float time)` and replace the right-arm line

```c
    draw_limb(&astro_arm, &base, v3_(0.40f, 0.40f, 0), -arm_swing * (player_on_foot(p) ? 1 : -1), -arm_splay);
```

with

```c
    if (fishing) draw_limb(&astro_arm, &base, v3_(0.40f, 0.40f, 0), ROD_ARM_SWING, 0);   /* holding the rod out front */
    else draw_limb(&astro_arm, &base, v3_(0.40f, 0.40f, 0), -arm_swing * (player_on_foot(p) ? 1 : -1), -arm_splay);
```

- [ ] **Step 4: Add the fishing draw functions**

After `draw_player` add:

```c
static void put_v(vertex *v, v3 p, v3 n, u32 col) {
    v->pos[0] = p.x; v->pos[1] = p.y; v->pos[2] = p.z;
    v->nrm[0] = n.x; v->nrm[1] = n.y; v->nrm[2] = n.z;
    v->uv[0] = v->uv[1] = 0;
    v->col[0] = (col >> 24) & 0xFF; v->col[1] = (col >> 16) & 0xFF; v->col[2] = (col >> 8) & 0xFF; v->col[3] = col & 0xFF;
}

/* Draws a CPU-filled buffer at the world origin. */
static void draw_dynamic(vertex *vbo, int count, const C3D_Material *mat) {
    if (!count) return;
    GSPGPU_FlushDataCache(vbo, count * sizeof(vertex));
    gpu_mesh g = { vbo, count, 0, 0 };
    C3D_Mtx m; Mtx_Identity(&m);
    draw(&g, &m, mat);
}

/* Rod, bobber and fish: opaque, lit. */
static void draw_fishing_solid(const fishing *f) {
    if (!fishing_active(f)) return;
    rod_frame r; fishing_rod_frame(f, &r);
    for (int i = 0; i < 4; i++) {   /* stiff butt, whippy tip: each segment bends a little more */
        v3 a = fishing_rod_point(&r, i / 4.0f), b = fishing_rod_point(&r, (i + 1) / 4.0f);
        v3 up = v3_norm(v3_sub(b, a)), fwd = v3_any_perp(up), right = v3_cross(up, fwd);
        float th = 0.05f - 0.03f * (i + 0.5f) / 4.0f;
        C3D_Mtx m; mtx_basis(&m, right, up, fwd, v3_scale(v3_add(a, b), 0.5f));
        Mtx_Scale(&m, th, v3_len(v3_sub(b, a)) * 1.03f, th);
        draw(&rod_seg, &m, &mat_lit);
    }
    if (f->bobber_visible) {
        C3D_Mtx m;
        mtx_translate(&m, v3_add(f->bobber, v3_scale(f->bobber_up, 0.03f)));
        draw(&bobber_top, &m, &mat_lit);
        mtx_translate(&m, v3_sub(f->bobber, v3_scale(f->bobber_up, 0.03f)));
        draw(&bobber_bot, &m, &mat_lit);
    }
    if (f->fish_visible) {
        v3 up = f->fish_up, fwd = f->fish_fwd, right = v3_cross(up, fwd);
        C3D_Mtx m; mtx_basis(&m, right, up, fwd, f->fish_pos);
        Mtx_Scale(&m, f->fish_scale, f->fish_scale, f->fish_scale);
        draw(&fish_mesh, &m, &mat_lit);
    }
}

static v3 bezier(v3 a, v3 c, v3 b, float t) {
    float u = 1 - t;
    return v3_add(v3_add(v3_scale(a, u * u), v3_scale(c, 2 * u * t)), v3_scale(b, t * t));
}

/* Line and splash: translucent, unlit; call in the blended pass with culling off. */
static void draw_fishing_soft(const fishing *f, const camera *c) {
    if (!fishing_active(f)) return;
    if (f->line_visible) {
        rod_frame r; fishing_rod_frame(f, &r);
        v3 a = fishing_rod_point(&r, 1.0f), b = f->bobber;
        float d = v3_len(v3_sub(b, a));
        float sag = 0.3f * (1.0f + (0.05f - 1.0f) * f->line_taut);
        v3 ctrl = v3_add(v3_scale(v3_add(a, b), 0.5f), v3_scale(f->bobber_up, -d * sag));
        const u32 col = 0xFFFFFF8Cu;   /* white, alpha 140 */
        int n = 0; v3 prev = a;
        for (int i = 0; i < LINE_SEGS; i++) {
            v3 q = bezier(a, ctrl, b, (i + 1) / (float)LINE_SEGS);
            v3 seg = v3_sub(q, prev), to_cam = v3_sub(c->pos, prev);
            v3 side = v3_cross(seg, to_cam);
            side = v3_len(side) > 1e-6f ? v3_norm(side) : v3_any_perp(v3_norm(seg));
            /* about 1.5 px wide whatever the distance */
            v3 s0 = v3_scale(side, 0.0033f * v3_len(to_cam)), s1 = v3_scale(side, 0.0033f * v3_len(v3_sub(c->pos, q)));
            v3 nrm = v3_norm(to_cam);
            v3 p0 = v3_add(prev, s0), p1 = v3_sub(prev, s0), p2 = v3_add(q, s1), p3 = v3_sub(q, s1);
            put_v(&line_vbo[n++], p0, nrm, col); put_v(&line_vbo[n++], p1, nrm, col); put_v(&line_vbo[n++], p2, nrm, col);
            put_v(&line_vbo[n++], p1, nrm, col); put_v(&line_vbo[n++], p3, nrm, col); put_v(&line_vbo[n++], p2, nrm, col);
            prev = q;
        }
        draw_dynamic(line_vbo, n, &mat_glow);
    }
    if (f->splash_t < 0.5f) {
        float k = f->splash_t / 0.5f;
        float rad = 0.2f + 1.0f * k, w = 0.06f + 0.04f * k;
        u32 col = 0xFFFFFF00u | (u32)(200 * (1.0f - k));
        v3 up = f->bobber_up, u = v3_any_perp(up), v = v3_cross(up, u);
        int n = 0;
        for (int i = 0; i < 8; i++) {
            float a0 = 6.2831853f * i / 8, a1 = 6.2831853f * (i + 1) / 8;
            v3 d0 = v3_add(v3_scale(u, cosf(a0)), v3_scale(v, sinf(a0))), d1 = v3_add(v3_scale(u, cosf(a1)), v3_scale(v, sinf(a1)));
            v3 o0 = v3_add(f->splash_pos, v3_scale(d0, rad)), o1 = v3_add(f->splash_pos, v3_scale(d1, rad));
            v3 i0 = v3_add(f->splash_pos, v3_scale(d0, rad - w)), i1 = v3_add(f->splash_pos, v3_scale(d1, rad - w));
            put_v(&splash_vbo[n++], i0, up, col); put_v(&splash_vbo[n++], o0, up, col); put_v(&splash_vbo[n++], o1, up, col);
            put_v(&splash_vbo[n++], i0, up, col); put_v(&splash_vbo[n++], o1, up, col); put_v(&splash_vbo[n++], i1, up, col);
        }
        draw_dynamic(splash_vbo, n, &mat_glow);
    }
}
```

- [ ] **Step 5: Call them from `render_scene`**

Change the signature to `void render_scene(const world *w, const player *p, const camera *c, const fishing *f, float time)`. Replace `draw_player(p, c, time);` with:

```c
    draw_player(p, c, fishing_active(f), time);
    draw_fishing_solid(f);
```

In the seas block, after the `for` loop that draws `water[i]` (still inside the blended, cull-none, no-depth-write state) add:

```c
    draw_fishing_soft(f, c);
```

- [ ] **Step 6: Build check is deferred to Task 8** (main.c does not compile until it passes the `fishing`). Commit now:

```bash
cd /c/3ds && git add orbit/source/render.h orbit/source/render.c
git commit -m "Orbit: draw the fishing rod, line, bobber, fish and splash

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: top-screen fishing HUD

**Files:**
- Create: `source/fishhud.h`, `source/fishhud.c`

- [ ] **Step 1: Write `source/fishhud.h`**

```c
/* Top-screen fishing overlay drawn with citro2d: tension bar, HOOK! prompt, CAUGHT banner. */
#ifndef FISHHUD_H
#define FISHHUD_H
#include <citro2d.h>
#include "fishing.h"

void fishhud_draw(C3D_RenderTarget *top, const fishing *f, float dt);

#endif
```

- [ ] **Step 2: Write `source/fishhud.c`**

```c
#include "fishhud.h"
#include "font.h"
#include <math.h>

#define BAR_W 120
#define BAR_H 5
#define BAR_Y 150
#define CX 200

static float bar_a;   /* bar fade 0..1 */

static u32 rgba(int r, int g, int b, float a) { return C2D_Color32(r, g, b, (u8)(a * 255)); }
static void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.0f, w, h, c); }

void fishhud_draw(C3D_RenderTarget *top, const fishing *f, float dt) {
    C2D_SceneBegin(top);
    float goal = f->state == FISH_FIGHT ? 1.0f : 0.0f;
    bar_a += (goal - bar_a) * (1.0f - expf(-9.0f * dt));
    if (bar_a > 0.02f) {
        float ten = f->fight.tension / 100.0f;
        if (ten > 1) ten = 1;
        float hot = ten < 0.75f ? 0 : (ten - 0.75f) / 0.25f;   /* orange to red past 3/4 */
        int r = 255 + (240 - 255) * hot, g = 192 + (66 - 192) * hot, b = 64 + (46 - 64) * hot;
        u32 fill = rgba(r, g, b, bar_a), brk = hot > 0 ? rgba(240, 66, 46, bar_a) : rgba(255, 192, 64, bar_a);
        float x0 = CX - BAR_W / 2 + sinf(f->time * 46.0f) * 3.2f * f->bar_shake, y0 = BAR_Y;
        rect(x0, y0, BAR_W, BAR_H, rgba(0, 0, 0, 0.42f * bar_a));
        rect(x0, y0, BAR_W * ten, BAR_H, fill);
        /* corner brackets */
        float l = x0 - 4, rr = x0 + BAR_W + 4, t = y0 - 4, bt = y0 + BAR_H + 4;
        rect(l, t, 7, 2, brk); rect(l, t, 2, 7, brk);
        rect(rr - 7, t, 7, 2, brk); rect(rr - 2, t, 2, 7, brk);
        rect(l, bt - 2, 7, 2, brk); rect(l, bt - 7, 2, 7, brk);
        rect(rr - 7, bt - 2, 7, 2, brk); rect(rr - 2, bt - 7, 2, 7, brk);
    }
    if (f->state == FISH_STRIKE && fmodf(f->time, 0.25f) < 0.125f) {
        const char *s = "HOOK!  TAP R";
        font_draw(s, CX - font_width(s, 2) / 2, BAR_Y - 4, 2, rgba(255, 192, 64, 1));
    }
    if (f->banner_t > 0) {
        float a = f->banner_t < 0.5f ? f->banner_t / 0.5f : 1.0f;
        font_draw(f->banner, CX - font_width(f->banner, 2) / 2, 60, 2, rgba(255, 192, 64, a));
    }
    C2D_Flush();
}
```

- [ ] **Step 3: Commit**

```bash
cd /c/3ds && git add orbit/source/fishhud.h orbit/source/fishhud.c
git commit -m "Orbit: top-screen fishing HUD: tension bar, hook prompt, catch banner

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: bottom-screen HUD: status, controls, tally

**Files:**
- Modify: `source/hud.h`, `source/hud.c`

- [ ] **Step 1: Signature**

In `source/hud.h` add `#include "fishing.h"` and change the declaration to:

```c
void hud_draw(C3D_RenderTarget *bottom, const world *w, const player *p, const camera *c, const fishing *f, float fps, int gyro_settling);
```

- [ ] **Step 2: Status word and tally**

In `source/hud.c` change the function signature to match, and replace the status `snprintf` with:

```c
    const char *status = fishing_active(f) ? "FISHING" : (p->swimming ? "SWIMMING" : (p->grounded ? (p->in_water ? "WADING" : "GROUNDED") : (p->thrusting ? "THRUST" : "FLYING")));
    if (f->caught > 0) snprintf(buf, sizeof buf, "%s  %s  FISH %d", b->name, status, f->caught);
    else snprintf(buf, sizeof buf, "%s  %s", b->name, status);
```

- [ ] **Step 3: Controls**

Replace the `if (player_on_foot(p)) { ... } else { ... }` controls block with:

```c
    if (fishing_active(f)) {
        font_draw("R  HOLD: CAST / REEL", 8, 166, 1.5f, dim);
        font_draw("TAP R: HOOK A BITE", 8, 180, 1.5f, dim);
        font_draw("DPAD DOWN: PUT AWAY", 8, 194, 1.5f, dim);
    } else if (player_on_foot(p)) {
        font_draw("PAD  LOOK       A  WALK", 8, 166, 1.5f, dim);
        font_draw("X  JUMP   DPAD DOWN: ROD", 8, 180, 1.5f, dim);
        if (c->gyro) font_draw("MOVE THE 3DS TO LOOK AROUND", 8, 194, 1.5f, dim);
        else if (c->first_person) font_draw("(GYRO BUTTON: MOTION LOOK)", 8, 194, 1.5f, dim);
        else font_draw("(VIEW BUTTON: FIRST PERSON)", 8, 194, 1.5f, dim);
    } else {
        font_draw("PAD  LOOK       L/R ROLL", 8, 166, 1.5f, dim);
        font_draw("X  UP     B  DOWN", 8, 180, 1.5f, dim);
        font_draw("A  BOOST  Y  BRAKE", 8, 194, 1.5f, dim);
    }
```

- [ ] **Step 4: Commit**

```bash
cd /c/3ds && git add orbit/source/hud.h orbit/source/hud.c
git commit -m "Orbit: bottom screen shows fishing status, rod controls and the catch tally

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: wire it up in `main.c` and build

**Files:**
- Modify: `source/main.c`

- [ ] **Step 1: Includes and the hand frame**

After `#include "gimbal.h"` add:

```c
#include "fishing.h"
#include "fishhud.h"
```

After `pad_axis` add:

```c
/* Where the rod is held: the right hand of the model in third person, the lower right of the
   view in first person, with the plane the rod swings in. */
static void rod_hand(const player *p, const camera *c, v3 *butt, v3 *fwd, v3 *up) {
    if (c->fp > 0.5f) {
        v3 right = v3_norm(v3_cross(c->view, c->view_up));
        *butt = v3_add(v3_add(v3_add(c->pos, v3_scale(right, 0.38f)), v3_scale(c->view_up, -0.32f)), v3_scale(c->view, 0.25f));
        *fwd = c->view; *up = c->view_up;
    } else {
        v3 u = p->body_up, f = v3_tangent(p->body_fwd, u);
        f = v3_len(f) < 1e-3f ? v3_any_perp(u) : v3_norm(f);
        v3 right = v3_cross(u, f);
        /* the raised right arm: shoulder (0.40, 0.40) plus 0.74 of arm swung forward */
        *butt = v3_add(v3_add(v3_add(p->pos, v3_scale(right, 0.40f)), v3_scale(u, 0.20f)), v3_scale(f, 0.70f));
        *fwd = f; *up = u;
    }
}
```

- [ ] **Step 2: State and input**

After `hud_init();` add:

```c
    fishing fish; fishing_init(&fish);
```

Change the SELECT line to:

```c
        if (kDown & KEY_SELECT) { player_spawn(&p, &w, BODY_START); camera_init(&cam, &p); fishing_init(&fish); }
```

In the on-foot input branch, after `in.jump_pressed = ...` add:

```c
            if (fishing_active(&fish)) in.face = cam.fwd;   /* square up to the cast */
```

After `camera_update(&cam, &w, &p, &cin, dt);` add:

```c
        {
            fishing_input fin;
            memset(&fin, 0, sizeof fin);
            fin.equip_pressed = (kDown & KEY_DDOWN) != 0;
            fin.r_down = (kHeld & KEY_R) != 0;
            fin.r_pressed = (kDown & KEY_R) != 0;
            fin.cast_dir = cam.view;
            rod_hand(&p, &cam, &fin.hand, &fin.hand_fwd, &fin.hand_up);
            fishing_update(&fish, &w, &p, &fin, dt);
        }
```

- [ ] **Step 3: Draw calls**

Change `render_scene(&w, &p, &cam, w.t);` to `render_scene(&w, &p, &cam, &fish, w.t);`.
After `gimbal_draw(render_top_target(), &w, &p, &cam);` add `fishhud_draw(render_top_target(), &fish, dt);`.
Change the `hud_draw` call to `hud_draw(bottom, &w, &p, &cam, &fish, fps, sensors_calibrating());`.

- [ ] **Step 4: Host tests, then the 3DS build**

Run: `bash tests/run_tests.sh` — expected all pass.
Run: `C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; cd /c/3ds/orbit && make 2>&1 | tail -20"`
Expected: no errors, `orbit.3dsx` and `orbit.elf` updated. Fix any warnings-as-errors (the 3DS build uses `-Wall` without `-Werror`, but keep it clean).

- [ ] **Step 5: Package the CIA**

Run: `C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; cd /c/3ds/orbit && bash build_cia.sh 2>&1 | tail -3"`
Expected: `orbit.cia` listed.

- [ ] **Step 6: Commit**

```bash
cd /c/3ds && git add orbit/source/main.c
git commit -m "Orbit: fishing rod wired to d-pad down and R; hand frame for first and third person

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: playtest notes for the console

Not code. Hand the user `orbit.cia` and this checklist; tune from their answers:

1. D-pad down on the beach: rod swings up in the right hand, bobber drops to dangle. If the arm points backwards, flip `ROD_ARM_SWING` in `render.c`.
2. Tap R: short cast (~4 units). Hold R 2 s: long cast (~14). If short/long feel wrong, change `CAST_SPEED_MIN/MAX` in `fishing.c`.
3. Bobber lands with a ring splash and floats; the line sags. Hold R: line straightens for half a second, then the bobber slides home and hops up to the tip.
4. Wait 5–14 s: a fish spirals up and grabs the float; `HOOK! TAP R` blinks; the bobber sinks and shivers. Tap R.
5. Fight: tension bar under the crosshair fills while holding; let go on the shove (run) and it drains. Snap = rod whips forward, bobber pops back up. Land = fish towed in, `CAUGHT!` banner, `FISH 1` on the bottom screen.
6. Jump or fly: rod disappears.
