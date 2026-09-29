/* Procedural meshes, portable C. Triangle lists, optionally indexed. */
#ifndef MESH_H
#define MESH_H
#include <stdint.h>
#include "world.h"

typedef struct {
    float pos[3];
    float nrm[3];
    uint8_t col[4];   /* r g b a */
} vertex;

typedef struct {
    vertex *v;
    int count;        /* vertices */
    uint16_t *idx;    /* NULL for a plain triangle list */
    int idx_count;
} mesh;

/* Body i's surface: an indexed sphere displaced by the terrain height, coloured by biome. */
mesh mesh_planet(const world *w, int i, int rings, int segs);
/* Trees, rocks and shards scattered over body i, in coordinates relative to its centre. */
mesh mesh_props(const world *w, int i);
/* Axis-aligned box centred on the origin. */
mesh mesh_box(float sx, float sy, float sz, uint32_t col);
/* Unit cone: base circle of radius 1 at y = 0, apex at y = 1. */
mesh mesh_cone(int segs, uint32_t col);
/* Astronaut parts in model space: +y up, +z forward, origin at the middle of the body.
   The body includes helmet, torso and jetpack. Arm and leg hang from the origin (their pivot)
   along -y and are meant to be drawn at the shoulder / hip. */
typedef enum { ASTRO_BODY, ASTRO_ARM, ASTRO_LEG } astro_part;
mesh mesh_astronaut(astro_part part);
/* n small triangles scattered on a sphere of the given radius. */
mesh mesh_stars(int n, float radius, float size, uint32_t seed);
void mesh_free(mesh *m);

#endif
