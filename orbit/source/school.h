/* Ambient fish cruising under the sea surface near the player, so the water looks alive and
   the fish sizes can be judged. Plain C, no 3DS headers. Positions are relative to the body's
   centre so they ride along with it for free. */
#ifndef SCHOOL_H
#define SCHOOL_H
#include "world.h"
#include "player.h"

#define SCHOOL_N 20

typedef struct {
    int alive;
    v3 rel;          /* from the body's centre */
    v3 fwd;          /* unit, tangent to the sphere */
    float depth;     /* below the sea surface */
    float speed, scale, phase, turn;
} school_fish;

typedef struct {
    school_fish f[SCHOOL_N];
    int body;        /* body the fish belong to, -1 for none */
    uint32_t rng;
} school;

void school_init(school *s);
void school_update(school *s, const world *w, const player *p, float dt);
/* Drawing basis for fish i: position, up and forward (with the swimming wiggle). */
void school_fish_basis(const school *s, const world *w, int i, v3 *pos, v3 *up, v3 *fwd);

#endif
