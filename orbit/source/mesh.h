/* Procedural meshes, portable C. Triangle lists, optionally indexed. */
#ifndef MESH_H
#define MESH_H
#include <stdint.h>
#include "world.h"

typedef struct {
    float pos[3];
    float nrm[3];
    uint8_t col[4];   /* r g b a */
    float uv[2];
} vertex;

typedef struct {
    vertex *v;
    int count;        /* vertices */
    uint16_t *idx;    /* NULL for a plain triangle list */
    int idx_count;
} mesh;

/* Body i's surface: an indexed sphere displaced by its height grid, coloured by biome,
   with tiling texture coordinates for a detail texture. */
mesh mesh_planet(const world *w, int i);
/* Translucent sea surface for body i at its sea radius (indexed sphere). Empty if no sea. */
mesh mesh_water(const world *w, int i, uint32_t col);
/* The drifting ripple layer just above body i's sea (indexed, textured). Empty if no sea. */
mesh mesh_water_ripple(const world *w, int i);
/* Foam ribbon along body i's shoreline (triangle list, translucent). Empty if no sea. */
mesh mesh_foam(const world *w, int i);
/* Faint nebula bands on a sphere of the given radius, for additive drawing behind the stars. */
mesh mesh_nebula(float radius, uint32_t seed);
/* Plain indexed sphere of the given radius, radial normals, one colour, no texcoords. */
mesh mesh_sphere(float radius, int rings, int segs, uint32_t col);
/* The same with texture coordinates tiling `rep` times pole to pole (2 * rep around). */
mesh mesh_sphere_uv(float radius, int rings, int segs, uint32_t col, float rep);
/* Trees, rocks and shards on body i whose direction falls in the given octant
   (0..7, bits: x negative, y negative, z negative), relative to the body's centre. */
mesh mesh_props(const world *w, int i, int octant);
/* Grass tufts on body i in one of GRASS_CELLS lat/long cells; drawn double sided. */
#define GRASS_CELLS 32
mesh mesh_grass(const world *w, int i, int cell);
/* Unit direction of the centre of a grass cell. */
v3 mesh_grass_cell_dir(int cell);
/* Axis-aligned box centred on the origin. */
mesh mesh_box(float sx, float sy, float sz, uint32_t col);
/* Unit cone: base circle of radius 1 at y = 0, apex at y = 1. */
mesh mesh_cone(int segs, uint32_t col);
/* Astronaut parts in model space: +y up, +z forward, origin at the middle of the body.
   The body includes helmet, torso and jetpack. Arm and leg hang from the origin (their pivot)
   along -y and are meant to be drawn at the shoulder / hip. */
typedef enum { ASTRO_BODY, ASTRO_ARM, ASTRO_LEG } astro_part;
mesh mesh_astronaut(astro_part part);
/* A small fish in model space: +z is the head, +y up, about 0.5 long, origin mid-body. */
mesh mesh_fish(void);
/* Octahedron of the given radius at the origin (fireflies, gems). */
mesh mesh_gem(float r, uint32_t col);
/* Building blocks for meshes made elsewhere (cavemesh.c). The mesh must have room. */
void mesh_put(mesh *m, v3 p, v3 n, uint32_t col);
void mesh_put_uv(mesh *m, v3 p, v3 n, uint32_t col, float u, float v);
/* Body i at 1/div the grid resolution, for drawing from far away. */
mesh mesh_planet_lod(const world *w, int i, int div);
/* One triangle wound so its normal points away from `inside`, flat shaded. */
void mesh_tri(mesh *m, v3 a, v3 b, v3 c, v3 inside, uint32_t col);
/* One triangle with the given per-vertex normals, wound so the face agrees with them. */
void mesh_tri_n(mesh *m, v3 a, v3 na, v3 b, v3 nb, v3 c, v3 nc, uint32_t ca, uint32_t cb, uint32_t cc);
/* Octahedron with radii along a basis, appended. */
void mesh_octa(mesh *m, v3 c, v3 r, v3 u, v3 f, float rx, float ry, float rz, uint32_t col);
/* Houses and their aliens, in the house's local frame (x right, y up, z forward, origin at the
   pad). style 0: the clay dome hut and the tall three-eyed VESK; 1: the teal lodge and the
   round one-eyed MOMO. The glow mesh holds the parts drawn emissive (windows, eyes). */
mesh mesh_house(int style);
mesh mesh_house_glow(int style);
mesh mesh_alien(int style);
mesh mesh_alien_glow(int style);
/* n small triangles scattered on a sphere of the given radius. */
mesh mesh_stars(int n, float radius, float size, uint32_t seed);
void mesh_free(mesh *m);

#endif
