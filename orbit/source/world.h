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

/* A tree, rock or shard cluster standing on a body. Placed once by world_init so the mesh and
   the player's collision agree. */
typedef struct {
    v3 dir;          /* unit direction from the body's centre */
    int kind;        /* 0 tree, 1 rock, 2 shard cluster */
    float s;         /* size: trees 0.8..1.6, rocks and shards 0.7..1.4 */
    float crown;     /* radius of the widest part, keeps neighbours apart */
    float hit_r;     /* radius the player collides with */
    float height;    /* height of the solid part above its base */
    uint32_t k;      /* placement number, seeds the look */
} prop;

typedef struct { int trees, rocks, shards; uint32_t rock_a, rock_b; float rock_scale; } prop_plan;

/* A moon's cave network, carved out of its rock: capsule tunnels, blob caverns and the
   hollow zero-g core. The moon's whole surface, caves included, is one density field
   (moon.c) meshed as one surface, and the player collides with that same field. */
#define CAVE_SEG_MAX 180
#define CAVE_BLOB_MAX 28
#define CAVE_MOUTH_MAX 4
#define CORE_ZONE 6.0f   /* the zero-g zone reaches this far outside the core cavern's wall */
typedef struct { v3 a, b; float r; } cave_seg;
typedef struct { v3 c; float r; } cave_blob;
typedef struct {
    int enabled;
    cave_seg seg[CAVE_SEG_MAX]; int nseg;
    cave_blob blob[CAVE_BLOB_MAX]; int nblob;
    v3 mouth[CAVE_MOUTH_MAX]; int nmouth;   /* unit directions of the tunnel mouths */
    float core_r;        /* the core cavern; gravity is zero inside */
    float cell;          /* voxel size the surface is meshed at */
} cavenet;


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
    prop *props; int nprops;   /* placed by world_init */
    cavenet caves;             /* moons only: the carved cave network */
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
/* Index of the body whose frame the player should use at p: the innermost body whose sphere
   of influence contains p (the sun when none does). */
int world_dominant(const world *w, v3 p);
/* Acceleration of body i's frame: the pull of every other body at i's centre. Subtract it from
   the gravity felt near i so the orbit does not read as a push; only the tides remain. */
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
/* How many props of each kind a terrain gets, and its rock colours. */
prop_plan world_prop_plan(terrain_kind t);
/* World position of a prop's base (a little below the surface so it never floats). */
v3 world_prop_base(const world *w, int i, const prop *q);

#endif
