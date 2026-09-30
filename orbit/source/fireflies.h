/* Fireflies: little blinking lights that drift over the night side of a planet near the player
   and never cross onto the day side. Plain C, no 3DS headers. */
#ifndef FIREFLIES_H
#define FIREFLIES_H
#include "world.h"
#include "player.h"

#define FIREFLY_N 40

typedef struct {
    int alive;
    v3 dir;          /* unit direction from the body's centre */
    v3 vel;          /* drift, tangent, units per second */
    float h0, h;     /* height above the terrain: rest and current */
    float phase, rate;   /* blink */
    float life;      /* 0..1 fade in and out */
    int dying;
} firefly;

typedef struct {
    firefly f[FIREFLY_N];
    int body;        /* -1 when none are out */
    float t;
    uint32_t rng;
} fireflies;

void fireflies_init(fireflies *s);
void fireflies_update(fireflies *s, const world *w, const player *p, float dt);
/* World position and brightness 0..1 of firefly i (0 when it is out). */
float fireflies_get(const fireflies *s, const world *w, int i, v3 *pos);

#endif
