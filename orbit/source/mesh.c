#include "mesh.h"
#include "noise.h"
#include <stdlib.h>
#include <string.h>

static void set_col(vertex *v, uint32_t c) {
    v->col[0] = (c >> 24) & 0xFF; v->col[1] = (c >> 16) & 0xFF;
    v->col[2] = (c >> 8) & 0xFF;  v->col[3] = c & 0xFF;
}

static void put(vertex *v, v3 p, v3 n, uint32_t c) {
    v->pos[0] = p.x; v->pos[1] = p.y; v->pos[2] = p.z;
    v->nrm[0] = n.x; v->nrm[1] = n.y; v->nrm[2] = n.z;
    set_col(v, c);
}

static v3 vpos(const vertex *v) { return v3_(v->pos[0], v->pos[1], v->pos[2]); }

static v3 sphere_pt(int i, int rings, int j, int segs) {
    float th = (float)M_PI * i / rings, ph = 2.0f * (float)M_PI * j / segs;
    return v3_(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
}

/* ---- planets: indexed sphere displaced by the terrain ---- */

mesh mesh_planet(const world *w, int bi, int rings, int segs) {
    const body *b = &w->b[bi];
    mesh m;
    m.count = (rings + 1) * segs;
    m.v = calloc(m.count, sizeof(vertex));
    m.idx = malloc(rings * segs * 6 * sizeof(uint16_t));

    for (int i = 0; i <= rings; i++) {
        for (int j = 0; j < segs; j++) {
            v3 dir = sphere_pt(i, rings, j, segs);
            float raw = world_height_raw(w, bi, dir);
            float h = (b->has_sea && raw < 0) ? 0 : raw;
            put(&m.v[i * segs + j], v3_scale(dir, b->radius + h), v3_(0, 0, 0), world_color(w, bi, dir, raw));
        }
    }
    uint16_t *o = m.idx;
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segs; j++) {
            int j1 = (j + 1) % segs;
            uint16_t a = i * segs + j, bb = (i + 1) * segs + j, c = (i + 1) * segs + j1, d = i * segs + j1;
            /* a c b and a d c are counter-clockwise seen from outside; the pole rows
               collapse one triangle of each quad to a sliver, so skip those */
            if (i < rings - 1) { *o++ = a; *o++ = c; *o++ = bb; }
            if (i > 0)         { *o++ = a; *o++ = d; *o++ = c; }
        }
    }
    m.idx_count = (int)(o - m.idx);
    /* smooth normals from the faces */
    for (int t = 0; t < m.idx_count; t += 3) {
        vertex *va = &m.v[m.idx[t]], *vb = &m.v[m.idx[t + 1]], *vc = &m.v[m.idx[t + 2]];
        v3 pa = vpos(va), n = v3_cross(v3_sub(vpos(vb), pa), v3_sub(vpos(vc), pa));
        vertex *vs[3] = { va, vb, vc };
        for (int k = 0; k < 3; k++) { vs[k]->nrm[0] += n.x; vs[k]->nrm[1] += n.y; vs[k]->nrm[2] += n.z; }
    }
    for (int k = 0; k < m.count; k++) {
        v3 n = v3_(m.v[k].nrm[0], m.v[k].nrm[1], m.v[k].nrm[2]);
        n = v3_len(n) > 1e-6f ? v3_norm(n) : v3_norm(vpos(&m.v[k]));
        m.v[k].nrm[0] = n.x; m.v[k].nrm[1] = n.y; m.v[k].nrm[2] = n.z;
    }
    return m;
}

/* ---- props: small flat-shaded shapes appended to a growing triangle list ---- */

/* Emits one triangle wound so its normal points away from `inside`. */
static void emit_tri(mesh *m, v3 a, v3 b, v3 c, v3 inside, uint32_t col) {
    v3 n = v3_cross(v3_sub(b, a), v3_sub(c, a));
    v3 centroid = v3_scale(v3_add(v3_add(a, b), c), 1.0f / 3.0f);
    if (v3_dot(n, v3_sub(centroid, inside)) < 0) { v3 t = b; b = c; c = t; n = v3_scale(n, -1); }
    n = v3_norm(n);
    put(&m->v[m->count++], a, n, col);
    put(&m->v[m->count++], b, n, col);
    put(&m->v[m->count++], c, n, col);
}

/* Box with half sizes along the given basis. 36 vertices. */
static void emit_box(mesh *m, v3 c, v3 r, v3 u, v3 f, float hx, float hy, float hz, uint32_t col) {
    v3 p[8];
    for (int k = 0; k < 8; k++) {
        float sx = (k & 1) ? hx : -hx, sy = (k & 2) ? hy : -hy, sz = (k & 4) ? hz : -hz;
        p[k] = v3_add(v3_add(v3_add(c, v3_scale(r, sx)), v3_scale(u, sy)), v3_scale(f, sz));
    }
    static const int faces[6][4] = { {0,1,3,2}, {4,5,7,6}, {0,1,5,4}, {2,3,7,6}, {0,2,6,4}, {1,3,7,5} };
    for (int k = 0; k < 6; k++) {
        emit_tri(m, p[faces[k][0]], p[faces[k][1]], p[faces[k][2]], c, col);
        emit_tri(m, p[faces[k][0]], p[faces[k][2]], p[faces[k][3]], c, col);
    }
}

/* Octahedron with radii along the basis. 24 vertices. */
static void emit_octa(mesh *m, v3 c, v3 r, v3 u, v3 f, float rx, float ry, float rz, uint32_t col) {
    v3 top = v3_add(c, v3_scale(u, ry)), bot = v3_sub(c, v3_scale(u, ry));
    v3 e[4] = { v3_add(c, v3_scale(r, rx)), v3_add(c, v3_scale(f, rz)), v3_sub(c, v3_scale(r, rx)), v3_sub(c, v3_scale(f, rz)) };
    for (int k = 0; k < 4; k++) {
        emit_tri(m, top, e[k], e[(k + 1) & 3], c, col);
        emit_tri(m, bot, e[k], e[(k + 1) & 3], c, col);
    }
}

static uint32_t shade(uint32_t col, float k) {
    float r = ((col >> 24) & 0xFF) * k, g = ((col >> 16) & 0xFF) * k, b = ((col >> 8) & 0xFF) * k;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
}

typedef struct { int trees, rocks, shards; uint32_t rock_col; } prop_plan;

static prop_plan plan_for(terrain_kind t) {
    prop_plan p = { 0, 0, 0, 0x787068FF };
    switch (t) {
    case TERRAIN_VERDANT: p.trees = 300; p.rocks = 150; p.rock_col = 0x7C7468FF; break;
    case TERRAIN_EMBER:   p.rocks = 260; p.rock_col = 0x5E2D1EFF; break;
    case TERRAIN_FROST:   p.rocks = 80; p.shards = 180; p.rock_col = 0xAAB8C8FF; break;
    default: break;
    }
    return p;
}

mesh mesh_props(const world *w, int bi) {
    const body *b = &w->b[bi];
    prop_plan plan = plan_for(b->terrain);
    mesh m = { 0, 0, 0, 0 };
    int cap = plan.trees * 60 + plan.rocks * 24 + plan.shards * 24;
    if (cap == 0) return m;
    m.v = calloc(cap, sizeof(vertex));

    int want[3] = { plan.trees, plan.rocks, plan.shards };
    uint32_t seed = b->seed * 7919u + 3u;
    int k = 0;
    for (int kind = 0; kind < 3; kind++) {
        int placed = 0, guard = 0;
        while (placed < want[kind] && guard++ < 20000) {
            k++;
            v3 dir = v3_(noise_hashf(k, 0, kind, seed) * 2 - 1, noise_hashf(k, 1, kind, seed) * 2 - 1, noise_hashf(k, 2, kind, seed) * 2 - 1);
            float l = v3_len(dir);
            if (l > 1 || l < 0.1f) continue;
            dir = v3_scale(dir, 1.0f / l);
            float raw = world_height_raw(w, bi, dir);
            float t = b->amp > 0 ? raw / b->amp : 0;
            if (kind == 0 && (t < 0.08f || t > 0.8f)) continue;   /* trees: not beach, not snow */
            if (kind != 0 && b->has_sea && raw < 0.02f) continue;  /* nothing on water */

            /* local frame, rotated by a random angle for variety */
            v3 up = dir, t1 = v3_any_perp(up);
            t1 = v3_rotate(t1, up, noise_hashf(k, 3, kind, seed) * 6.2832f);
            v3 t2 = v3_cross(up, t1);
            v3 base = v3_scale(dir, b->radius + world_height(w, bi, dir));
            float s = 0.7f + noise_hashf(k, 4, kind, seed) * 0.7f;   /* size 0.7..1.4 */

            if (kind == 0) {
                uint32_t greens[3] = { 0x3E8C3AFF, 0x4E9E3CFF, 0x2F7A38FF };
                uint32_t canopy = greens[noise_hash(k, 5, kind, seed) % 3];
                emit_box(&m, v3_add(base, v3_scale(up, 0.8f * s)), t1, up, t2, 0.2f * s, 0.8f * s, 0.2f * s, 0x6B4A2AFF);
                emit_octa(&m, v3_add(base, v3_scale(up, (1.6f + 1.0f) * s)), t1, up, t2, 1.3f * s, 1.2f * s, 1.3f * s, canopy);
            } else if (kind == 1) {
                float rx = (0.5f + noise_hashf(k, 6, kind, seed) * 1.3f) * s;
                float ry = (0.4f + noise_hashf(k, 7, kind, seed) * 1.0f) * s;
                float rz = (0.5f + noise_hashf(k, 8, kind, seed) * 1.3f) * s;
                uint32_t col = shade(plan.rock_col, 0.85f + noise_hashf(k, 9, kind, seed) * 0.3f);
                emit_octa(&m, v3_add(base, v3_scale(up, ry * 0.3f)), t1, up, t2, rx, ry, rz, col);
            } else {
                float ry = (1.4f + noise_hashf(k, 6, kind, seed) * 1.8f) * s;
                float rr = (0.3f + noise_hashf(k, 7, kind, seed) * 0.35f) * s;
                v3 tilt = v3_rotate(up, t1, (noise_hashf(k, 8, kind, seed) - 0.5f) * 0.5f);
                v3 tr = v3_norm(v3_cross(tilt, t2)), tf = v3_cross(tr, tilt);
                emit_octa(&m, v3_add(base, v3_scale(tilt, ry * 0.6f)), tr, tilt, tf, rr, ry, rr, 0xC8E0F4FF);
            }
            placed++;
        }
    }
    return m;
}

mesh mesh_box(float sx, float sy, float sz, uint32_t col) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(36, sizeof(vertex));
    emit_box(&m, v3_(0, 0, 0), v3_(1, 0, 0), v3_(0, 1, 0), v3_(0, 0, 1), sx / 2, sy / 2, sz / 2, col);
    return m;
}

static uint32_t rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s) { return (rnd(s) >> 8) / 16777216.0f; }

mesh mesh_stars(int n, float radius, float size, uint32_t seed) {
    mesh m = { 0, 0, 0, 0 };
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

void mesh_free(mesh *m) { free(m->v); free(m->idx); m->v = 0; m->idx = 0; m->count = 0; m->idx_count = 0; }
