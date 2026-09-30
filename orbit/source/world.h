/* The sun, the planets and their moons. Planets ride fixed circular orbits around the sun and
   moons ride tilted circular orbits around their planet (all kinematic). Every body has
   procedural terrain: a height grid over the sphere shared by rendering and collision. */
#ifndef WORLD_H
#define WORLD_H
#include <stdint.h>
#include "vec.h"

#define BODY_COUNT 7
#define BODY_SUN 0
#define BODY_EMBER 1
#define BODY_VERDANT 2
#define BODY_FROST 3
#define BODY_CINDER 4   /* Ember's moon */
#define BODY_FERN 5     /* Verdant's moon */
#define BODY_SLEET 6    /* Frost's moon */
#define BODY_START BODY_VERDANT   /* the player spawns on this body */

typedef enum { TERRAIN_SUN, TERRAIN_EMBER, TERRAIN_VERDANT, TERRAIN_FROST, TERRAIN_MOON } terrain_kind;

typedef struct {
    const char *name;
    float radius;
    float mu;            /* G * mass: surface gravity is mu / radius^2 */
    int parent;          /* body this one orbits: -1 for the sun, BODY_SUN for planets, a planet for moons */
    float orbit_r;       /* 0 for the sun */
    float orbit_phase;   /* radians at t = 0 */
    float tilt;          /* orbit plane tilt about the x axis, radians (moons) */
    float omega;         /* orbital angular speed, derived from the parent's mu */
    int emissive;
    terrain_kind terrain;
    float amp;           /* terrain height amplitude, 0 for a perfect sphere */
    float freq;          /* noise cells across the unit sphere */
    float sea_level;     /* height of the sea or ice sheet, relative to radius */
    int liquid;          /* 1: water you sink into below sea_level; 0: ice you walk on (flattened) */
    int rings, segs;     /* terrain grid resolution, shared by the mesh and collision */
    uint32_t seed;
    uint32_t atmo;       /* sky colour 0xRRGGBBAA at full daylight, alpha = strength; 0 for none */
    float atmo_h;        /* atmosphere thickness above the surface */
    float *grid;         /* (rings + 1) * segs heights, filled by world_init */
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
/* Acceleration of body i's frame: the pull of everything it orbits, at the centres they orbit
   from. Subtract it from the gravity felt near i so a circular orbit does not read as a push. */
v3 world_frame_accel(const world *w, int i);

/* Terrain. dir is a unit vector from the body's centre. */
float world_height_raw(const world *w, int i, v3 dir);   /* the noise itself, unclamped */
float world_grid_height(const world *w, int i, int ring, int seg);   /* a mesh vertex height */
float world_height(const world *w, int i, v3 dir);       /* interpolated over the mesh triangles */
float world_surface(const world *w, int i, v3 dir);      /* radius + height */
/* Radius of the liquid sea surface, or -1 if the body has no liquid sea. */
float world_sea_radius(const world *w, int i);
/* Unit direction of body i's grid vertex. */
v3 world_grid_dir(const world *w, int i, int ring, int seg);
/* Vertex colour for a point on body i's surface, 0xRRGGBBAA. */
uint32_t world_color(const world *w, int i, v3 dir, float raw_h);

#endif
