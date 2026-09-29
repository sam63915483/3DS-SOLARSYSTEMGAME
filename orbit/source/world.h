/* The sun and the planets. Planets ride fixed circular orbits (kinematic). */
#ifndef WORLD_H
#define WORLD_H
#include <stdint.h>
#include "vec.h"

#define BODY_COUNT 4
#define BODY_SUN 0
#define BODY_START 2   /* the player spawns on this body */

typedef struct {
    const char *name;
    float radius;
    float mu;            /* G * mass: surface gravity is mu / radius^2 */
    float orbit_r;       /* 0 for the sun */
    float orbit_phase;   /* radians at t = 0 */
    float omega;         /* orbital angular speed, derived from the sun's mu */
    uint32_t color_a, color_b;  /* two checker tones, 0xRRGGBBAA */
    int emissive;
    v3 pos, prev, vel;   /* world position now, last frame, and velocity */
} body;

typedef struct {
    body b[BODY_COUNT];
    float t;
} world;

void world_init(world *w);
void world_update(world *w, float dt);
/* Gravitational acceleration at p from body i. */
v3 world_gravity_from(const world *w, int i, v3 p);
/* Sum of gravity from every body at p. */
v3 world_gravity_at(const world *w, v3 p);
/* Index of the body whose pull is strongest at p. */
int world_dominant(const world *w, v3 p);

#endif
