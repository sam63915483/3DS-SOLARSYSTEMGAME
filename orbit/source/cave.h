/* Moon caves: layout, and the collision the player and camera use inside them. Plain C. */
#ifndef CAVE_H
#define CAVE_H
#include "world.h"

/* Lays out body i's cave (moons only). Called by world_init before the terrain is baked, since
   the pit around the mouth is part of the terrain. */
void cave_setup(world *w, int i);

/* Where rel (from the body's centre) is: 0 outside, 1 in the tunnel, 2 in the core cavern. When
   inside and within margin of a wall, n is the inward wall normal and pen how far to move along
   it to be margin clear; otherwise pen is 0. The top of the shaft is open. */
int cave_inside(const cave *c, v3 rel, float margin, v3 *n, float *pen);

/* Distance straight down (along -up) from rel to the tunnel wall, or -1 if there is none within
   max_s. Used to keep feet on the tunnel floor. */
float cave_floor_below(const cave *c, v3 rel, v3 up, float max_s);

/* Angle between two unit vectors. */
static inline float cave_angle(v3 a, v3 b) {
    float d = v3_dot(a, b);
    if (d > 1) d = 1;
    if (d < -1) d = -1;
    return acosf(d);
}

#endif
