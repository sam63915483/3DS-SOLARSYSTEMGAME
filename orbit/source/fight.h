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
