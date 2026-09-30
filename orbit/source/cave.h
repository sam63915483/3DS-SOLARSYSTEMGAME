/* Moon caves: layout, the spline axis, and the collision the player and camera use inside.
   Plain C, no 3DS headers. */
#ifndef CAVE_H
#define CAVE_H
#include "world.h"

/* Lays out body i's cave (moons only). Called by world_init before the terrain is baked, since
   the pit around the mouth is part of the terrain. */
void cave_setup(world *w, int i);

/* Where rel (from the body's centre) is: 0 outside, 1 in the tunnel, 2 in the core cavern. When
   inside and within margin of a wall or the flat floor, n is the inward normal and pen how far
   to move along it to be margin clear; otherwise pen is 0. The mouth is open. */
int cave_inside(const cave *c, v3 rel, float margin, v3 *n, float *pen);

/* Distance straight down (along -up) from rel to the tunnel floor or wall, or -1 if none
   within max_s. Keeps feet on the floor. */
float cave_floor_below(const cave *c, v3 rel, v3 up, float max_s);

/* Distance from rel to the tunnel axis (the sampled polyline), with the nearest sample index. */
float cave_axis_dist(const cave *c, v3 rel, int *nearest);

/* Frame of the tunnel at sample k: axis direction, two perpendiculars carried along the axis
   so rings never twist, and the local "down" as an angle in the (t1, t2) plane. */
void cave_frames(const cave *c, v3 *u, v3 *t1, v3 *t2, float *down_ang);

/* Radius of the tunnel wall at angle a (from t1 toward t2) on ring k: tube_r, with the floor
   flattened. Roughness is added by the mesh, not here. */
float cave_wall_radius(const cave *c, float a, float down_ang);

/* Angle between two unit vectors. */
static inline float cave_angle(v3 a, v3 b) {
    float d = v3_dot(a, b);
    if (d > 1) d = 1;
    if (d < -1) d = -1;
    return acosf(d);
}

#endif
