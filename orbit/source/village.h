/* The two houses on Verdant and the aliens who live in them. Plain C, no 3DS headers. */
#ifndef VILLAGE_H
#define VILLAGE_H
#include "world.h"
#include "player.h"

/* Places the houses near the spawn on body i (before the terrain is baked: their pads flatten it). */
void village_setup(world *w, int i);
/* Keeps the player out of the walls and the aliens; walking through a doorway is fine. */
void village_collide(const world *w, player *p);
/* The aliens turn to look at a player who comes close. */
void village_update(world *w, const player *p, float dt);
/* The line the nearest alien says when the player is within earshot, or NULL. */
const char *village_talk(const world *w, const player *p);
/* Local frame of house k: origin at the pad, x right, y up, z forward (world). */
void village_frame(const world *w, int k, v3 *origin, v3 *right, v3 *up, v3 *fwd);

/* House footprints in local units, shared by the meshes and the collision. */
#define DOME_R      4.2f
#define DOME_DOOR   0.34f   /* half angle of the doorway, radians */
#define LODGE_HX    3.6f
#define LODGE_HZ    3.0f
#define LODGE_WALL  3.4f
#define LODGE_ROOF  5.0f    /* height at the back */
#define LODGE_DOOR  1.0f    /* half width */
#define LODGE_DOOR_H 2.4f

#endif
