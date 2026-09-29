#include "mesh.h"
#include "vec.h"
#include <stdlib.h>

static void set_col(vertex *v, uint32_t c) {
    v->col[0] = (c >> 24) & 0xFF; v->col[1] = (c >> 16) & 0xFF;
    v->col[2] = (c >> 8) & 0xFF;  v->col[3] = c & 0xFF;
}

static void put(vertex *v, v3 p, v3 n, uint32_t c) {
    v->pos[0] = p.x; v->pos[1] = p.y; v->pos[2] = p.z;
    v->nrm[0] = n.x; v->nrm[1] = n.y; v->nrm[2] = n.z;
    set_col(v, c);
}

static v3 sphere_pt(int i, int rings, int j, int segs) {
    float th = (float)M_PI * i / rings, ph = 2.0f * (float)M_PI * j / segs;
    return v3_(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
}

mesh mesh_sphere(float radius, int rings, int segs, uint32_t col_a, uint32_t col_b) {
    mesh m;
    m.count = rings * segs * 6;
    m.v = calloc(m.count, sizeof(vertex));
    vertex *o = m.v;
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segs; j++) {
            uint32_t c = ((i + j) & 1) ? col_a : col_b;
            v3 a = sphere_pt(i, rings, j, segs), b = sphere_pt(i + 1, rings, j, segs);
            v3 cc = sphere_pt(i + 1, rings, j + 1, segs), d = sphere_pt(i, rings, j + 1, segs);
            /* a c b and a d c are counter-clockwise seen from outside */
            put(o++, v3_scale(a, radius), a, c);
            put(o++, v3_scale(cc, radius), cc, c);
            put(o++, v3_scale(b, radius), b, c);
            put(o++, v3_scale(a, radius), a, c);
            put(o++, v3_scale(d, radius), d, c);
            put(o++, v3_scale(cc, radius), cc, c);
        }
    }
    return m;
}

mesh mesh_box(float sx, float sy, float sz, uint32_t col) {
    static const float n[6][3] = { {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
    mesh m;
    m.count = 36;
    m.v = calloc(m.count, sizeof(vertex));
    vertex *o = m.v;
    float h[3] = { sx / 2, sy / 2, sz / 2 };
    for (int f = 0; f < 6; f++) {
        v3 nn = v3_(n[f][0], n[f][1], n[f][2]);
        /* build a tangent basis so the quad winds CCW when seen from outside */
        v3 u = v3_any_perp(nn), v = v3_cross(nn, u);
        v3 c = v3_(nn.x * h[0], nn.y * h[1], nn.z * h[2]);
        v3 uu = v3_(u.x * h[0], u.y * h[1], u.z * h[2]);
        v3 vv = v3_(v.x * h[0], v.y * h[1], v.z * h[2]);
        v3 p0 = v3_sub(v3_sub(c, uu), vv), p1 = v3_sub(v3_add(c, uu), vv);
        v3 p2 = v3_add(v3_add(c, uu), vv), p3 = v3_add(v3_sub(c, uu), vv);
        put(o++, p0, nn, col); put(o++, p1, nn, col); put(o++, p2, nn, col);
        put(o++, p0, nn, col); put(o++, p2, nn, col); put(o++, p3, nn, col);
    }
    return m;
}

static uint32_t rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s) { return (rnd(s) >> 8) / 16777216.0f; }

mesh mesh_stars(int n, float radius, float size, uint32_t seed) {
    mesh m;
    m.count = n * 3;
    m.v = calloc(m.count, sizeof(vertex));
    vertex *o = m.v;
    for (int i = 0; i < n; i++) {
        v3 d;
        do { d = v3_(frand(&seed) * 2 - 1, frand(&seed) * 2 - 1, frand(&seed) * 2 - 1); }
        while (v3_len(d) > 1 || v3_len(d) < 0.1f);
        d = v3_norm(d);
        v3 u = v3_any_perp(d), v = v3_cross(d, u);
        float s = size * (0.5f + frand(&seed));
        uint8_t br = 140 + (uint8_t)(frand(&seed) * 115);
        uint32_t c = ((uint32_t)br << 24) | ((uint32_t)br << 16) | ((uint32_t)(br - 20) << 8) | 0xFF;
        v3 p = v3_scale(d, radius), nn = v3_scale(d, -1);
        put(o++, v3_add(p, v3_scale(u, s)), nn, c);
        put(o++, v3_add(p, v3_scale(v, s)), nn, c);
        put(o++, v3_sub(p, v3_scale(v3_add(u, v), s * 0.7f)), nn, c);
    }
    return m;
}

void mesh_free(mesh *m) { free(m->v); m->v = 0; m->count = 0; }
