/* Procedural meshes: non-indexed triangle lists, portable C. */
#ifndef MESH_H
#define MESH_H
#include <stdint.h>

typedef struct {
    float pos[3];
    float nrm[3];
    uint8_t col[4];   /* r g b a */
} vertex;

typedef struct {
    vertex *v;
    int count;
} mesh;

/* UV sphere with a two-tone checker in the vertex colours, CCW from outside. */
mesh mesh_sphere(float radius, int rings, int segs, uint32_t col_a, uint32_t col_b);
/* Axis-aligned box centred on the origin. */
mesh mesh_box(float sx, float sy, float sz, uint32_t col);
/* n small triangles scattered on a sphere of the given radius. */
mesh mesh_stars(int n, float radius, float size, uint32_t seed);
void mesh_free(mesh *m);

#endif
