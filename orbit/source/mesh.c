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
    v->uv[0] = v->uv[1] = 0;
    set_col(v, c);
}

static v3 vpos(const vertex *v) { return v3_(v->pos[0], v->pos[1], v->pos[2]); }

static v3 sphere_pt(int i, int rings, int j, int segs) {
    float th = (float)M_PI * i / rings, ph = 2.0f * (float)M_PI * j / segs;
    return v3_(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
}

/* ---- indexed spheres ---- */

/* Indexed sphere grid. With uv_rep > 0 the seam column is duplicated so texture
   coordinates can tile uv_rep times around (2 * uv_rep along the equator). */
static mesh sphere_grid(int rings, int segs, float uv_rep, float (*radius_of)(const void *, v3, int, int),
                        uint32_t (*color_of)(const void *, v3, int, int), const void *ctx, int smooth_normals) {
    int cols = uv_rep > 0 ? segs + 1 : segs;
    mesh m;
    m.count = (rings + 1) * cols;
    m.v = calloc(m.count, sizeof(vertex));
    m.idx = malloc(rings * segs * 6 * sizeof(uint16_t));
    for (int i = 0; i <= rings; i++) {
        for (int j = 0; j < cols; j++) {
            v3 dir = sphere_pt(i, rings, j % segs, segs);
            vertex *v = &m.v[i * cols + j];
            put(v, v3_scale(dir, radius_of(ctx, dir, i, j % segs)), dir, color_of(ctx, dir, i, j % segs));
            if (uv_rep > 0) { v->uv[0] = 2.0f * uv_rep * j / segs; v->uv[1] = uv_rep * i / rings; }
        }
    }
    uint16_t *o = m.idx;
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segs; j++) {
            int j1 = uv_rep > 0 ? j + 1 : (j + 1) % segs;
            uint16_t a = i * cols + j, bb = (i + 1) * cols + j, c = (i + 1) * cols + j1, d = i * cols + j1;
            /* a c b and a d c are counter-clockwise seen from outside; the pole rows
               collapse one triangle of each quad to a sliver, so skip those */
            if (i < rings - 1) { *o++ = a; *o++ = c; *o++ = bb; }
            if (i > 0)         { *o++ = a; *o++ = d; *o++ = c; }
        }
    }
    m.idx_count = (int)(o - m.idx);
    if (smooth_normals) {
        for (int k = 0; k < m.count; k++) m.v[k].nrm[0] = m.v[k].nrm[1] = m.v[k].nrm[2] = 0;
        for (int t = 0; t < m.idx_count; t += 3) {
            vertex *va = &m.v[m.idx[t]], *vb = &m.v[m.idx[t + 1]], *vc = &m.v[m.idx[t + 2]];
            v3 pa = vpos(va), n = v3_cross(v3_sub(vpos(vb), pa), v3_sub(vpos(vc), pa));
            vertex *vs[3] = { va, vb, vc };
            for (int k = 0; k < 3; k++) { vs[k]->nrm[0] += n.x; vs[k]->nrm[1] += n.y; vs[k]->nrm[2] += n.z; }
        }
        if (uv_rep > 0) {
            /* the seam copies share one normal so the lighting does not show a line */
            for (int i = 0; i <= rings; i++) {
                vertex *a = &m.v[i * cols], *b = &m.v[i * cols + segs];
                for (int k = 0; k < 3; k++) { float s = a->nrm[k] + b->nrm[k]; a->nrm[k] = b->nrm[k] = s; }
            }
        }
        for (int k = 0; k < m.count; k++) {
            v3 n = v3_(m.v[k].nrm[0], m.v[k].nrm[1], m.v[k].nrm[2]);
            n = v3_len(n) > 1e-6f ? v3_norm(n) : v3_norm(vpos(&m.v[k]));
            m.v[k].nrm[0] = n.x; m.v[k].nrm[1] = n.y; m.v[k].nrm[2] = n.z;
        }
    }
    return m;
}

typedef struct { const world *w; int i; uint32_t col; float radius; } sphere_ctx;

static float planet_radius(const void *c, v3 dir, int ring, int seg) {
    const sphere_ctx *s = c; (void)dir;
    return s->w->b[s->i].radius + world_grid_height(s->w, s->i, ring, seg);
}
static uint32_t planet_color(const void *c, v3 dir, int ring, int seg) {
    const sphere_ctx *s = c; (void)ring; (void)seg;
    return world_color(s->w, s->i, dir, world_height_raw(s->w, s->i, dir));
}
static float flat_radius(const void *c, v3 dir, int ring, int seg) { (void)dir; (void)ring; (void)seg; return ((const sphere_ctx *)c)->radius; }
static uint32_t flat_color(const void *c, v3 dir, int ring, int seg) { (void)dir; (void)ring; (void)seg; return ((const sphere_ctx *)c)->col; }

mesh mesh_planet(const world *w, int i) {
    sphere_ctx c = { w, i, 0, 0 };
    float rep = w->b[i].emissive ? 0 : w->b[i].radius / 4.0f;   /* one repeat every ~12.5 units at the equator */
    mesh m = sphere_grid(w->b[i].rings, w->b[i].segs, rep, planet_radius, planet_color, &c, 1);
    if (w->b[i].cave.enabled) {
        /* open the tunnel mouth: drop every triangle with a corner inside the tunnel; the
           cave's skirt lies over the ragged edge that leaves */
        int out = 0;
        for (int t = 0; t + 2 < m.idx_count; t += 3) {
            int cut = 0;
            for (int k = 0; k < 3; k++) if (world_in_hole(w, i, v3_norm(vpos(&m.v[m.idx[t + k]])))) cut = 1;
            if (cut) continue;
            m.idx[out++] = m.idx[t]; m.idx[out++] = m.idx[t + 1]; m.idx[out++] = m.idx[t + 2];
        }
        m.idx_count = out;
    }
    return m;
}

mesh mesh_water(const world *w, int i, uint32_t col) {
    mesh empty = { 0, 0, 0, 0 };
    float r = world_sea_radius(w, i);
    if (r <= 0) return empty;
    sphere_ctx c = { w, i, col, r };
    return sphere_grid(32, 48, r / 5.0f, flat_radius, flat_color, &c, 0);
}

mesh mesh_sphere(float radius, int rings, int segs, uint32_t col) {
    sphere_ctx c = { 0, 0, col, radius };
    return sphere_grid(rings, segs, 0, flat_radius, flat_color, &c, 0);
}

mesh mesh_sphere_uv(float radius, int rings, int segs, uint32_t col, float rep) {
    sphere_ctx c = { 0, 0, col, radius };
    return sphere_grid(rings, segs, rep, flat_radius, flat_color, &c, 0);
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

/* Open-ended tapered octagonal tube along `up`: radius r0 at the base, r1 at height h. 16 tris. */
static void emit_taper(mesh *m, v3 base, v3 t1, v3 up, v3 t2, float r0, float r1, float h, uint32_t col) {
    v3 ring0[8], ring1[8];
    for (int k = 0; k < 8; k++) {
        float a = k * (float)M_PI / 4, ca = cosf(a), sa = sinf(a);
        ring0[k] = v3_add(base, v3_add(v3_scale(t1, r0 * ca), v3_scale(t2, r0 * sa)));
        ring1[k] = v3_add(v3_add(base, v3_scale(up, h)), v3_add(v3_scale(t1, r1 * ca), v3_scale(t2, r1 * sa)));
    }
    v3 inside = v3_add(base, v3_scale(up, h * 0.5f));
    for (int k = 0; k < 8; k++) {
        int k1 = (k + 1) & 7;
        emit_tri(m, ring0[k], ring0[k1], ring1[k1], inside, col);
        emit_tri(m, ring0[k], ring1[k1], ring1[k], inside, col);
    }
}

/* Cone without its base cap, 8 side tris, faces alternating between two tones. */
static void emit_cone_open(mesh *m, v3 base, v3 t1, v3 up, v3 t2, float r, float h, uint32_t ca, uint32_t cb) {
    v3 apex = v3_add(base, v3_scale(up, h)), inside = v3_add(base, v3_scale(up, h * 0.3f));
    v3 ring[8];
    for (int k = 0; k < 8; k++) {
        float a = k * (float)M_PI / 4;
        ring[k] = v3_add(base, v3_add(v3_scale(t1, r * cosf(a)), v3_scale(t2, r * sinf(a))));
    }
    for (int k = 0; k < 8; k++) emit_tri(m, ring[k], ring[(k + 1) & 7], apex, inside, (k & 1) ? ca : cb);
}

/* Lumpy low-poly blob: a 4x7 sphere with jittered radii, non-uniform scale, two tones. 42 tris. */
static void emit_blob(mesh *m, v3 c, v3 t1, v3 up, v3 t2, float rx, float ry, float rz, float jitter,
                      uint32_t ca, uint32_t cb, uint32_t seed) {
    const int rings = 4, segs = 7;
    v3 p[5][7];
    for (int i = 0; i <= rings; i++) {
        for (int j = 0; j < segs; j++) {
            v3 d = sphere_pt(i, rings, j, segs);
            float jit = 1.0f + (noise_hashf(i, j, 0, seed) - 0.5f) * 2.0f * jitter;
            if (i == 0 || i == rings) jit = 1.0f + (noise_hashf(i, 0, 0, seed) - 0.5f) * jitter;
            p[i][j] = v3_add(c, v3_add(v3_add(v3_scale(t1, d.x * rx * jit), v3_scale(up, d.y * ry * jit)), v3_scale(t2, d.z * rz * jit)));
        }
    }
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segs; j++) {
            int j1 = (j + 1) % segs;
            uint32_t col = ((i + j) & 1) ? ca : cb;
            if (i < rings - 1) emit_tri(m, p[i][j], p[i + 1][j1], p[i + 1][j], c, col);
            if (i > 0) emit_tri(m, p[i][j], p[i][j1], p[i + 1][j1], c, col);
        }
    }
}

/* A tall spruce: tapered trunk and four stacked open cones. ~144 vertices. */
static void emit_spruce(mesh *m, v3 base, v3 t1, v3 up, v3 t2, float s, uint32_t bark, uint32_t ca, uint32_t cb, float canopy_scale) {
    emit_taper(m, base, t1, up, t2, 0.36f * s, 0.20f * s, 3.0f * s, bark);
    float cs = canopy_scale;
    emit_cone_open(m, v3_add(base, v3_scale(up, 1.8f * s)), t1, up, t2, 2.7f * s * cs, 3.2f * s, ca, cb);
    emit_cone_open(m, v3_add(base, v3_scale(up, 3.7f * s)), t1, up, t2, 2.1f * s * cs, 2.8f * s, ca, cb);
    emit_cone_open(m, v3_add(base, v3_scale(up, 5.5f * s)), t1, up, t2, 1.5f * s * cs, 2.4f * s, ca, cb);
    emit_cone_open(m, v3_add(base, v3_scale(up, 7.1f * s)), t1, up, t2, 0.9f * s * cs, 2.0f * s, ca, cb);
}

static int octant_of(v3 d) { return (d.x < 0 ? 1 : 0) | (d.y < 0 ? 2 : 0) | (d.z < 0 ? 4 : 0); }

mesh mesh_props(const world *w, int bi, int octant) {
    const body *b = &w->b[bi];
    prop_plan plan = world_prop_plan(b->terrain);
    mesh m = { 0, 0, 0, 0 };
    int cap = plan.trees * 180 + plan.rocks * 130 + plan.shards * 80;
    if (cap == 0 || !b->nprops) return m;
    m.v = calloc(cap, sizeof(vertex));
    uint32_t seed = b->seed * 7919u + 3u;

    /* the props were placed by world_init; this octant draws its share */
    for (int i = 0; i < b->nprops; i++) {
        const prop *q = &b->props[i];
        if (octant_of(q->dir) != octant) continue;
        int k = (int)q->k, kind = q->kind;
        float s = q->s;
        {
            /* local frame, rotated by a random angle for variety */
            v3 up = q->dir, t1 = v3_any_perp(up);
            t1 = v3_rotate(t1, up, noise_hashf(k, 3, kind, seed) * 6.2832f);
            v3 t2 = v3_cross(up, t1);
            v3 base = v3_sub(world_prop_base(w, bi, q), b->pos);   /* the mesh is in body space */
            uint32_t pseed = seed ^ (uint32_t)(k * 2654435761u);

            if (kind == 0) {
                uint32_t bark = shade(0x6B4A2AFF, 0.85f + noise_hashf(k, 6, kind, seed) * 0.3f);
                float tone = 0.9f + noise_hashf(k, 7, kind, seed) * 0.25f;
                if (b->terrain == TERRAIN_FROST) {
                    /* snowy spruce: blue-green with white-capped faces */
                    emit_spruce(&m, base, t1, up, t2, s * 1.1f, shade(0x4A3A30FF, tone), shade(0x3E7C6AFF, tone), 0xE8F0F8FF, 1.0f);
                } else if (b->terrain == TERRAIN_EMBER) {
                    /* charred spruce: dark trunk, sparse rust foliage */
                    emit_spruce(&m, base, t1, up, t2, s, shade(0x2A1E1AFF, tone), shade(0x5A2A1EFF, tone), shade(0x3E1C14FF, tone), 0.6f);
                } else if (noise_hashf(k, 5, kind, seed) < 0.6f) {
                    uint32_t ga = shade(0x2F7A38FF, tone), gb = shade(ga, 0.8f);
                    emit_spruce(&m, base, t1, up, t2, s * 1.2f, bark, ga, gb, 1.0f);
                } else {
                    uint32_t ga = shade(0x4E9E3CFF, tone), gb = shade(ga, 0.78f);
                    emit_taper(&m, base, t1, up, t2, 0.42f * s, 0.26f * s, 4.2f * s, bark);
                    emit_blob(&m, v3_add(base, v3_scale(up, 6.0f * s)), t1, up, t2, 3.0f * s, 2.5f * s, 3.0f * s, 0.18f, ga, gb, pseed);
                }
            } else if (kind == 1) {
                float rx = (0.5f + noise_hashf(k, 6, kind, seed) * 1.2f) * s * plan.rock_scale;
                float ry = (0.4f + noise_hashf(k, 7, kind, seed) * 0.8f) * s * plan.rock_scale;
                float rz = (0.5f + noise_hashf(k, 8, kind, seed) * 1.2f) * s * plan.rock_scale;
                float tone = 0.85f + noise_hashf(k, 9, kind, seed) * 0.3f;
                emit_blob(&m, v3_add(base, v3_scale(up, ry * 0.35f)), t1, up, t2, rx, ry, rz, 0.28f,
                          shade(plan.rock_a, tone), shade(plan.rock_b, tone), pseed);
            } else {
                /* a cluster of three tilted shards */
                for (int c = 0; c < 3; c++) {
                    float ry = (1.5f + noise_hashf(k, 6 + c, kind, seed) * 3.0f) * s;
                    float rr = (0.3f + noise_hashf(k, 9 + c, kind, seed) * 0.4f) * s;
                    v3 off = v3_add(v3_scale(t1, (noise_hashf(k, 12 + c, kind, seed) - 0.5f) * 2.2f * s),
                                    v3_scale(t2, (noise_hashf(k, 15 + c, kind, seed) - 0.5f) * 2.2f * s));
                    v3 tilt = v3_rotate(v3_rotate(up, t1, (noise_hashf(k, 18 + c, kind, seed) - 0.5f) * 0.6f),
                                        t2, (noise_hashf(k, 21 + c, kind, seed) - 0.5f) * 0.6f);
                    v3 tr = v3_norm(v3_cross(tilt, t2)), tf = v3_cross(tr, tilt);
                    emit_octa(&m, v3_add(v3_add(base, off), v3_scale(tilt, ry * 0.55f)), tr, tilt, tf, rr, ry, rr,
                              (c & 1) ? 0xC8E0F4FF : 0xA8C8E8FF);
                }
            }
        }
    }
    return m;
}

/* ---- grass: 32 lat/long cells so only the nearby ones are drawn ---- */

static int grass_cell_of(v3 d) {
    float y = d.y; if (y > 1) y = 1; if (y < -1) y = -1;
    float th = acosf(y), ph = atan2f(d.z, d.x);
    if (ph < 0) ph += 2.0f * (float)M_PI;
    int lb = (int)(th / (float)M_PI * 4); if (lb > 3) lb = 3;
    int lo = (int)(ph / (2.0f * (float)M_PI) * 8); if (lo > 7) lo = 7;
    return lb * 8 + lo;
}

v3 mesh_grass_cell_dir(int cell) {
    float th = (float)M_PI * ((cell / 8) + 0.5f) / 4, ph = 2.0f * (float)M_PI * ((cell % 8) + 0.5f) / 8;
    return v3_(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
}

mesh mesh_grass(const world *w, int bi, int cell) {
    const body *b = &w->b[bi];
    mesh m = { 0, 0, 0, 0 };
    int total = b->terrain == TERRAIN_VERDANT ? 6000 : 0;
    if (!total) return m;
    /* a cell holds at most a third of the tufts (the equator cells are the largest) */
    m.v = calloc(total / 3 * 15 + 15, sizeof(vertex));
    uint32_t seed = b->seed * 104729u + 5u;
    int placed = 0, guard = 0, k = 0;
    while (placed < total && guard++ < 200000) {
        k++;
        v3 dir = v3_(noise_hashf(k, 0, 8, seed) * 2 - 1, noise_hashf(k, 1, 8, seed) * 2 - 1, noise_hashf(k, 2, 8, seed) * 2 - 1);
        float l = v3_len(dir);
        if (l > 1 || l < 0.1f) continue;
        dir = v3_scale(dir, 1.0f / l);
        float raw = world_height_raw(w, bi, dir);
        float lf = (raw - b->sea_level) / (b->amp - b->sea_level);
        if (lf < 0.07f || lf > 0.55f) continue;   /* grassland only */
        placed++;
        if (grass_cell_of(dir) != cell) continue;
        if (m.count + 15 > total / 3 * 15) break;

        v3 up = dir, t1 = v3_any_perp(up);
        t1 = v3_rotate(t1, up, noise_hashf(k, 3, 8, seed) * 6.2832f);
        v3 t2 = v3_cross(up, t1);
        v3 base = v3_scale(dir, world_surface(w, bi, dir) - 0.05f);
        float s = 0.8f + noise_hashf(k, 4, 8, seed) * 0.7f;
        uint32_t gc = shade(0x56A840FF, 0.75f + noise_hashf(k, 6, 8, seed) * 0.35f);
        for (int c = 0; c < 5; c++) {
            float a = c * 1.2566f + noise_hashf(k, 7 + c, 8, seed) * 0.8f;
            v3 side = v3_add(v3_scale(t1, cosf(a)), v3_scale(t2, sinf(a)));
            v3 lean = v3_add(v3_scale(t1, cosf(a + 1.57f)), v3_scale(t2, sinf(a + 1.57f)));
            float h = (0.9f + noise_hashf(k, 12 + c, 8, seed) * 0.9f) * s;
            v3 foot = v3_add(base, v3_scale(side, 0.28f * s));
            v3 tip = v3_add(v3_add(foot, v3_scale(up, h)), v3_scale(lean, 0.45f * s));
            emit_tri(&m, v3_sub(foot, v3_scale(lean, 0.22f * s)), v3_add(foot, v3_scale(lean, 0.22f * s)), tip, v3_sub(base, up), gc);
        }
    }
    return m;
}

/* ---- small shapes for the astronaut ---- */

static const v3 AX = { 1, 0, 0 }, AY = { 0, 1, 0 }, AZ = { 0, 0, 1 };

/* Octagonal prism: radius r around the y axis from y0 to y1. 16 side tris + 12 cap tris. */
static void emit_prism(mesh *m, v3 c, float r, float y0, float y1, uint32_t col) {
    v3 ring0[8], ring1[8];
    for (int k = 0; k < 8; k++) {
        float a = k * (float)M_PI / 4;
        ring0[k] = v3_add(c, v3_(r * cosf(a), y0, r * sinf(a)));
        ring1[k] = v3_add(c, v3_(r * cosf(a), y1, r * sinf(a)));
    }
    v3 inside = v3_add(c, v3_(0, (y0 + y1) / 2, 0));
    for (int k = 0; k < 8; k++) {
        int k1 = (k + 1) & 7;
        emit_tri(m, ring0[k], ring0[k1], ring1[k1], inside, col);
        emit_tri(m, ring0[k], ring1[k1], ring1[k], inside, col);
    }
    for (int k = 1; k < 7; k++) {
        emit_tri(m, ring0[0], ring0[k], ring0[k + 1], inside, col);
        emit_tri(m, ring1[0], ring1[k], ring1[k + 1], inside, col);
    }
}

/* Low-poly sphere via emit_tri. 6 rings x 10 segments. */
static void emit_sphere(mesh *m, v3 c, float r, uint32_t col) {
    const int rings = 6, segs = 10;
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segs; j++) {
            v3 a = v3_add(c, v3_scale(sphere_pt(i, rings, j, segs), r));
            v3 b = v3_add(c, v3_scale(sphere_pt(i + 1, rings, j, segs), r));
            v3 cc = v3_add(c, v3_scale(sphere_pt(i + 1, rings, j + 1, segs), r));
            v3 d = v3_add(c, v3_scale(sphere_pt(i, rings, j + 1, segs), r));
            if (i < rings - 1) emit_tri(m, a, cc, b, c, col);
            if (i > 0) emit_tri(m, a, d, cc, c, col);
        }
    }
}

static void emit_cone(mesh *m, v3 base, v3 axis, float r, float len, uint32_t col) {
    v3 up = v3_norm(axis), t1 = v3_any_perp(up), t2 = v3_cross(up, t1);
    v3 apex = v3_add(base, v3_scale(up, len)), inside = v3_add(base, v3_scale(up, len * 0.3f));
    v3 ring[8];
    for (int k = 0; k < 8; k++) {
        float a = k * (float)M_PI / 4;
        ring[k] = v3_add(base, v3_add(v3_scale(t1, r * cosf(a)), v3_scale(t2, r * sinf(a))));
    }
    for (int k = 0; k < 8; k++) emit_tri(m, ring[k], ring[(k + 1) & 7], apex, inside, col);
    for (int k = 1; k < 7; k++) emit_tri(m, ring[0], ring[k], ring[k + 1], inside, col);
}

mesh mesh_cone(int segs, uint32_t col) {
    (void)segs;
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(8 * 3 + 6 * 3, sizeof(vertex));
    emit_cone(&m, v3_(0, 0, 0), AY, 1.0f, 1.0f, col);
    return m;
}

#define SUIT   0xE8ECF2FF
#define SUIT2  0xC9D0DAFF
#define ACCENT 0xFF8A2AFF
#define DARK   0x2A2E38FF
#define METAL  0x8C949EFF
#define TANK   0xB8C0C8FF
#define VISOR  0x1E2A48FF

mesh mesh_astronaut(astro_part part) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(2000, sizeof(vertex));
    switch (part) {
    case ASTRO_BODY:
        /* torso, belt, chest panel */
        emit_box(&m, v3_(0, 0.15f, 0), AX, AY, AZ, 0.30f, 0.30f, 0.18f, SUIT);
        emit_box(&m, v3_(0, -0.14f, 0), AX, AY, AZ, 0.31f, 0.04f, 0.19f, DARK);
        emit_box(&m, v3_(0, 0.20f, 0.19f), AX, AY, AZ, 0.15f, 0.12f, 0.025f, METAL);
        emit_box(&m, v3_(0, 0.20f, 0.215f), AX, AY, AZ, 0.06f, 0.05f, 0.01f, ACCENT);
        /* neck ring, helmet, visor */
        emit_prism(&m, v3_(0, 0.44f, 0), 0.17f, 0, 0.06f, METAL);
        emit_sphere(&m, v3_(0, 0.63f, 0), 0.27f, SUIT);
        emit_box(&m, v3_(0, 0.65f, 0.20f), AX, AY, AZ, 0.16f, 0.09f, 0.06f, VISOR);
        /* jetpack: plate, two tanks, nozzle */
        emit_box(&m, v3_(0, 0.12f, -0.29f), AX, AY, AZ, 0.26f, 0.32f, 0.10f, METAL);
        emit_prism(&m, v3_(-0.14f, 0, -0.45f), 0.09f, -0.15f, 0.42f, TANK);
        emit_prism(&m, v3_(0.14f, 0, -0.45f), 0.09f, -0.15f, 0.42f, TANK);
        emit_prism(&m, v3_(-0.14f, 0, -0.45f), 0.05f, 0.42f, 0.50f, ACCENT);
        emit_prism(&m, v3_(0.14f, 0, -0.45f), 0.05f, 0.42f, 0.50f, ACCENT);
        emit_cone(&m, v3_(0, -0.20f, -0.34f), v3_(0, -1, 0), 0.09f, 0.10f, DARK);
        break;
    case ASTRO_ARM:
        emit_box(&m, v3_(0, -0.18f, 0), AX, AY, AZ, 0.09f, 0.18f, 0.09f, SUIT);
        emit_box(&m, v3_(0, -0.50f, 0), AX, AY, AZ, 0.08f, 0.16f, 0.08f, SUIT2);
        emit_box(&m, v3_(0, -0.74f, 0), AX, AY, AZ, 0.09f, 0.08f, 0.09f, ACCENT);
        break;
    case ASTRO_LEG:
        emit_box(&m, v3_(0, -0.20f, 0), AX, AY, AZ, 0.13f, 0.20f, 0.14f, SUIT);
        emit_box(&m, v3_(0, -0.56f, 0), AX, AY, AZ, 0.11f, 0.17f, 0.12f, SUIT2);
        emit_box(&m, v3_(0, -0.80f, 0.04f), AX, AY, AZ, 0.13f, 0.07f, 0.18f, DARK);
        break;
    }
    return m;
}

mesh mesh_box(float sx, float sy, float sz, uint32_t col) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(36, sizeof(vertex));
    emit_box(&m, v3_(0, 0, 0), v3_(1, 0, 0), v3_(0, 1, 0), v3_(0, 0, 1), sx / 2, sy / 2, sz / 2, col);
    return m;
}

mesh mesh_fish(void) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(24 + 6, sizeof(vertex));
    emit_octa(&m, v3_(0, 0, 0.02f), AX, AY, AZ, 0.07f, 0.10f, 0.26f, 0x6E9A6EFF);   /* body */
    /* tail fin: one vertical triangle, both faces */
    v3 root = v3_(0, 0, -0.22f), tt = v3_(0, 0.12f, -0.40f), tb = v3_(0, -0.12f, -0.40f);
    emit_tri(&m, root, tt, tb, v3_(0.2f, 0, -0.3f), 0x5A8058FF);
    emit_tri(&m, root, tb, tt, v3_(-0.2f, 0, -0.3f), 0x5A8058FF);
    return m;
}

mesh mesh_gem(float r, uint32_t col) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(24, sizeof(vertex));
    emit_octa(&m, v3_(0, 0, 0), AX, AY, AZ, r, r, r, col);
    return m;
}

void mesh_put(mesh *m, v3 p, v3 n, uint32_t col) { put(&m->v[m->count++], p, n, col); }
void mesh_tri(mesh *m, v3 a, v3 b, v3 c, v3 inside, uint32_t col) { emit_tri(m, a, b, c, inside, col); }
void mesh_tri_n(mesh *m, v3 a, v3 na, v3 b, v3 nb, v3 c, v3 nc, uint32_t ca, uint32_t cb, uint32_t cc) {
    v3 face = v3_cross(v3_sub(b, a), v3_sub(c, a));
    if (v3_dot(face, v3_add(v3_add(na, nb), nc)) < 0) {   /* wind to agree with the normals */
        v3 t = b; b = c; c = t;
        v3 tn = nb; nb = nc; nc = tn;
        uint32_t tc = cb; cb = cc; cc = tc;
    }
    put(&m->v[m->count++], a, na, ca);
    put(&m->v[m->count++], b, nb, cb);
    put(&m->v[m->count++], c, nc, cc);
}
void mesh_octa(mesh *m, v3 c, v3 r, v3 u, v3 f, float rx, float ry, float rz, uint32_t col) { emit_octa(m, c, r, u, f, rx, ry, rz, col); }

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
