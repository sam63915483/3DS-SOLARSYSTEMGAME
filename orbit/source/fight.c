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
    f->rng = seed * 2654435761u + 0x9E3779B9u;   /* scramble: nearby seeds must not roll alike */
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
