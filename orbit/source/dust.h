/* Dust puffs: kicked up on landing and by the jets near the ground. Plain C, no 3DS headers.
   Positions are world space; puffs ride along with the body they were spawned on. */
#ifndef DUST_H
#define DUST_H
#include "world.h"

#define DUST_N 48

typedef struct {
    int alive;
    v3 pos, vel;     /* world; vel relative to the body */
    float life, max_life, size;
    uint32_t col;    /* 0xRRGGBBAA at full life */
    int body;
} dust_puff;

typedef struct {
    dust_puff p[DUST_N];
    uint32_t rng;
    float jet_clock;
} dust;

void dust_init(dust *d);
/* A burst at pos (world) on body bi: n puffs spreading in the plane perpendicular to up. */
void dust_burst(dust *d, const world *w, int bi, v3 pos, v3 up, int n, float speed, float size);
void dust_update(dust *d, const world *w, float dt);
/* Alpha 0..1 of puff i now (0 when dead) and its size now. */
float dust_get(const dust *d, int i, float *size);

#endif
