/* Moons as one carved solid. The rock is a density field: the terrain heightfield minus a
   branching network of tunnels and caverns and a hollow core. Everything is meshed from that
   one field (surface nets), and the player and camera collide with the field itself, so what
   you see is exactly what you touch, mouths and all. Plain C, no 3DS headers. */
#ifndef MOON_H
#define MOON_H
#include "world.h"
#include "mesh.h"

/* Carves body i's cave network (moons only). Call after the terrain grid is baked. */
void moon_setup(world *w, int i);
/* The field at rel (from the body's centre): > 0 inside rock, < 0 in air, roughly a distance. */
float moon_field(const world *w, int i, v3 rel);
/* Gradient of the field: points into the rock. */
v3 moon_grad(const world *w, int i, v3 rel);
/* How far below the terrain surface rel is (positive underground). */
float moon_depth(const world *w, int i, v3 rel);
/* Meshes the whole moon, caves included, into 8 octant meshes in body space. */
void moon_build(const world *w, int i, mesh out[8]);
/* Glowing crystals on the cave walls and the orb at the heart. */
mesh moon_crystals(const world *w, int i);

#endif
