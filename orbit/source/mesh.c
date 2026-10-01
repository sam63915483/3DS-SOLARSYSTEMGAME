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
    return sphere_grid(w->b[i].rings, w->b[i].segs, rep, planet_radius, planet_color, &c, 1);
}

/* Sea colour by how much water is under it: pale and clear in the shallows, dark and opaque
   over the deeps. */
static uint32_t water_color(const void *c, v3 dir, int ring, int seg) {
    const sphere_ctx *s = c; (void)ring; (void)seg;
    float depth = s->radius - world_surface(s->w, s->i, dir);
    float t = depth / 14.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float r = 0x48 + (0x0C - 0x48) * t, g = 0x9C + (0x30 - 0x9C) * t, b = 0xC8 + (0x88 - 0xC8) * t;
    float a = 150 + (222 - 150) * t;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}

mesh mesh_water(const world *w, int i, uint32_t col) {
    mesh empty = { 0, 0, 0, 0 };
    float r = world_sea_radius(w, i);
    if (r <= 0) return empty;
    sphere_ctx c = { w, i, col, r };
    /* as fine as the terrain grid so the depth tint follows the coast */
    return sphere_grid(w->b[i].rings, w->b[i].segs, r / 5.0f, flat_radius, water_color, &c, 0);
}

/* The moving ripple layer over the sea: a faint white textured sphere that the renderer turns. */
mesh mesh_water_ripple(const world *w, int i) {
    mesh empty = { 0, 0, 0, 0 };
    float r = world_sea_radius(w, i);
    if (r <= 0) return empty;
    sphere_ctx c = { w, i, 0xFFFFFF3Cu, r + 0.02f };
    return sphere_grid(32, 48, r / 5.0f, flat_radius, flat_color, &c, 0);
}

/* Foam: a ribbon along the shoreline. Marching squares over the terrain grid against sea level
   gives the contour; each crossing becomes a short quad that hugs the ground on the land side
   and floats a hair above the sea on the water side. */
mesh mesh_foam(const world *w, int i) {
    mesh m = { 0, 0, 0, 0 };
    const body *b = &w->b[i];
    float sea = world_sea_radius(w, i);
    if (sea <= 0) return m;
    int cap = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) { m.v = calloc((size_t)cap * 6 + 6, sizeof(vertex)); m.count = 0; }
        for (int r = 0; r < b->rings; r++)
            for (int s = 0; s < b->segs; s++) {
                /* the cell's four corners, heights relative to sea level */
                int s1 = (s + 1) % b->segs;
                float h[4] = { world_grid_height(w, i, r, s) - b->sea_level, world_grid_height(w, i, r, s1) - b->sea_level,
                               world_grid_height(w, i, r + 1, s1) - b->sea_level, world_grid_height(w, i, r + 1, s) - b->sea_level };
                v3 d[4] = { world_grid_dir(w, i, r, s), world_grid_dir(w, i, r, s1), world_grid_dir(w, i, r + 1, s1), world_grid_dir(w, i, r + 1, s) };
                v3 cross[4]; int nc = 0;
                for (int e = 0; e < 4; e++) {
                    int e1 = (e + 1) & 3;
                    if ((h[e] > 0) == (h[e1] > 0)) continue;
                    float t = h[e] / (h[e] - h[e1]);
                    cross[nc++] = v3_norm(v3_lerp(d[e], d[e1], t));
                }
                if (nc < 2) continue;
                if (pass == 0) { cap += 2; continue; }
                /* one strip between the first two crossings, 1.4 wide across the contour */
                v3 a = cross[0], c = cross[1];
                v3 along = v3_sub(v3_scale(c, sea), v3_scale(a, sea));
                v3 mid = v3_norm(v3_add(a, c));
                v3 across = v3_norm(v3_cross(along, mid));   /* tangent, across the shoreline */
                /* which way is land: the higher side */
                v3 probe = v3_norm(v3_add(mid, v3_scale(across, 0.3f / sea)));
                if (world_surface(w, i, probe) < sea) across = v3_scale(across, -1.0f);
                v3 pts[4]; float alpha[4];
                v3 ends[2] = { a, c };
                for (int k = 0; k < 2; k++) {
                    v3 land = v3_norm(v3_add(ends[k], v3_scale(across, 0.5f / sea)));
                    v3 water = v3_norm(v3_add(ends[k], v3_scale(across, -0.9f / sea)));
                    float hl = world_surface(w, i, land); if (hl < sea) hl = sea;
                    pts[k * 2] = v3_scale(land, hl + 0.06f);
                    pts[k * 2 + 1] = v3_scale(water, sea + 0.05f);
                    alpha[k * 2] = 0.85f; alpha[k * 2 + 1] = 0.0f;   /* fades out to sea */
                }
                uint32_t cl = 0xF4F8FFD8u, cw = 0xF4F8FF00u;
                (void)alpha;
                mesh_tri_n(&m, pts[0], v3_norm(pts[0]), pts[1], v3_norm(pts[1]), pts[3], v3_norm(pts[3]), cl, cw, cw);
                mesh_tri_n(&m, pts[0], v3_norm(pts[0]), pts[3], v3_norm(pts[3]), pts[2], v3_norm(pts[2]), cl, cw, cl);
            }
    }
    return m;
}

/* A faint nebula on the star sphere: vertex alpha masks where the bands are, vertex colour
   tints them purple or teal, the cloud texture gives them their wisps. Drawn additively. */
static float nebula_radius(const void *c, v3 dir, int ring, int seg) { (void)dir; (void)ring; (void)seg; return ((const sphere_ctx *)c)->radius; }
static uint32_t nebula_color(const void *c, v3 dir, int ring, int seg) {
    const sphere_ctx *s = c; (void)ring; (void)seg;
    float band = noise_fbm(v3_add(v3_scale(dir, 1.6f), v3_(3.1f, 0.7f, 5.2f)), 3, s->col);
    float t = (band - 0.52f) / 0.14f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float hue = noise_value(v3_scale(dir, 1.1f), s->col + 9u);
    float r, g, b;
    if (hue < 0.5f) { r = 150; g = 80; b = 210; }   /* violet */
    else { r = 60; g = 170; b = 190; }               /* teal */
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)(t * 120);
}
mesh mesh_nebula(float radius, uint32_t seed) {
    sphere_ctx c = { NULL, 0, seed, radius };
    return sphere_grid(24, 36, 2.0f, nebula_radius, nebula_color, &c, 0);
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

void mesh_grass_cells(const world *w, int bi, mesh out[GRASS_CELLS]) {
    const body *b = &w->b[bi];
    for (int c = 0; c < GRASS_CELLS; c++) { mesh e = { 0, 0, 0, 0 }; out[c] = e; }
    int total = b->terrain == TERRAIN_VERDANT ? 10000 : 0;   /* the planet has 4x the area; grass is drawn near the camera only */
    if (!total) return;
    /* place every tuft once and sort them into cells; a cell keeps at most a third of the tufts
       (the equator cells are the largest) */
    int cap = total / 3;
    v3 *dirs = malloc(sizeof(v3) * (size_t)total);
    int *ks = malloc(sizeof(int) * (size_t)total);
    uint8_t *cells = malloc((size_t)total);
    int n = 0, per_cell[GRASS_CELLS] = { 0 };
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
        if (world_near_house(w, bi, dir, 9.0f)) continue;   /* not through the floors */
        placed++;
        int c = grass_cell_of(dir);
        if (per_cell[c] >= cap) continue;
        per_cell[c]++;
        dirs[n] = dir; ks[n] = k; cells[n] = (uint8_t)c; n++;
    }
    for (int c = 0; c < GRASS_CELLS; c++) out[c].v = calloc((size_t)per_cell[c] * 15 + 1, sizeof(vertex));

    for (int t = 0; t < n; t++) {
        mesh *m = &out[cells[t]];
        v3 dir = dirs[t];
        k = ks[t];
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
            emit_tri(m, v3_sub(foot, v3_scale(lean, 0.22f * s)), v3_add(foot, v3_scale(lean, 0.22f * s)), tip, v3_sub(base, up), gc);
        }
    }
    free(dirs); free(ks); free(cells);
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

/* ---- the village ---- */

/* A quad from four corners, both faces (walls are seen from inside and out). */
static void emit_quad2(mesh *m, v3 a, v3 b, v3 c, v3 d, uint32_t col) {
    v3 centre = v3_scale(v3_add(v3_add(a, b), v3_add(c, d)), 0.25f);
    v3 n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(d, a)));
    v3 out = v3_add(centre, n), in = v3_sub(centre, n);
    emit_tri(m, a, b, c, in, col); emit_tri(m, a, c, d, in, col);
    emit_tri(m, a, b, c, out, col); emit_tri(m, a, c, d, out, col);
}

#define CLAY   0xC8784AFF
#define CLAY2  0xA8603AFF
#define TEAL   0x4A9C9CFF
#define TEAL2  0x3A7C7CFF
#define TIMBER 0x6A4A32FF
#define PLINTH 0x50485AFF
#define FLOOR0 0xB89870FF
#define FLOOR1 0x70605AFF

mesh mesh_house(int style) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(VILLAGE_MESH_CAP, sizeof(vertex));
    if (style == 0) {
        /* the dome: a hemisphere of quads, both faces, a gap for the door in the lower rings,
           a darker band at the base and a round floor */
        const int rings = 6, segs = 16;
        for (int i = 0; i < rings; i++) {
            float t0 = (float)M_PI * 0.5f * i / rings, t1 = (float)M_PI * 0.5f * (i + 1) / rings;
            for (int j = 0; j < segs; j++) {
                float a0 = 2 * (float)M_PI * j / segs, a1 = 2 * (float)M_PI * (j + 1) / segs;
                float am = 0.5f * (a0 + a1);
                /* the door: straight out along +z, the two lowest rings */
                float door = am; while (door > (float)M_PI) door -= 2 * (float)M_PI;
                if (i < 2 && fabsf(door) < 0.34f) continue;
                v3 p00 = v3_(sinf(a0) * cosf(t0) * 4.2f, sinf(t0) * 4.2f, cosf(a0) * cosf(t0) * 4.2f);
                v3 p10 = v3_(sinf(a1) * cosf(t0) * 4.2f, sinf(t0) * 4.2f, cosf(a1) * cosf(t0) * 4.2f);
                v3 p11 = v3_(sinf(a1) * cosf(t1) * 4.2f, sinf(t1) * 4.2f, cosf(a1) * cosf(t1) * 4.2f);
                v3 p01 = v3_(sinf(a0) * cosf(t1) * 4.2f, sinf(t1) * 4.2f, cosf(a0) * cosf(t1) * 4.2f);
                uint32_t col = i == 0 ? CLAY2 : ((i + j) & 1 ? CLAY : shade(CLAY, 0.92f));
                emit_quad2(&m, p00, p10, p11, p01, col);
            }
        }
        /* the floor */
        for (int j = 0; j < segs; j++) {
            float a0 = 2 * (float)M_PI * j / segs, a1 = 2 * (float)M_PI * (j + 1) / segs;
            emit_tri(&m, v3_(0, 0.04f, 0), v3_(sinf(a0) * 4.1f, 0.04f, cosf(a0) * 4.1f), v3_(sinf(a1) * 4.1f, 0.04f, cosf(a1) * 4.1f), v3_(0, -1, 0), (j & 1) ? FLOOR0 : shade(FLOOR0, 0.9f));
        }
        /* a rug and a low table */
        emit_box(&m, v3_(-1.4f, 0.06f, -1.2f), AX, AY, AZ, 1.3f, 0.01f, 1.0f, 0x8A3A48FF);
        emit_box(&m, v3_(1.6f, 0.35f, -0.4f), AX, AY, AZ, 0.7f, 0.05f, 0.5f, TIMBER);
        emit_prism(&m, v3_(1.6f, 0, -0.4f), 0.12f, 0.0f, 0.3f, TIMBER);
    } else {
        /* the lodge: four walls (the front with a doorway), a slanting roof, a plinth, a chimney,
           a floor, a bunk and a stool */
        float hx = 3.6f, hz = 3.0f, wh = 3.4f, rh = 5.0f, dw = 1.0f, dh = 2.4f;
        /* side walls: trapezoids up to the sloping roof (high at the back, -z) */
        emit_quad2(&m, v3_(-hx, 0, -hz), v3_(-hx, 0, hz), v3_(-hx, wh, hz), v3_(-hx, rh, -hz), TEAL);
        emit_quad2(&m, v3_(hx, 0, -hz), v3_(hx, 0, hz), v3_(hx, wh, hz), v3_(hx, rh, -hz), TEAL2);
        emit_quad2(&m, v3_(-hx, 0, -hz), v3_(hx, 0, -hz), v3_(hx, rh, -hz), v3_(-hx, rh, -hz), TEAL);   /* back */
        /* front: two panels beside the door and a lintel above it */
        emit_quad2(&m, v3_(-hx, 0, hz), v3_(-dw, 0, hz), v3_(-dw, wh, hz), v3_(-hx, wh, hz), TEAL2);
        emit_quad2(&m, v3_(dw, 0, hz), v3_(hx, 0, hz), v3_(hx, wh, hz), v3_(dw, wh, hz), TEAL2);
        emit_quad2(&m, v3_(-dw, dh, hz), v3_(dw, dh, hz), v3_(dw, wh, hz), v3_(-dw, wh, hz), TEAL2);
        /* door frame */
        emit_box(&m, v3_(-dw - 0.08f, dh * 0.5f, hz), AX, AY, AZ, 0.08f, dh * 0.5f, 0.12f, TIMBER);
        emit_box(&m, v3_(dw + 0.08f, dh * 0.5f, hz), AX, AY, AZ, 0.08f, dh * 0.5f, 0.12f, TIMBER);
        emit_box(&m, v3_(0, dh + 0.08f, hz), AX, AY, AZ, dw + 0.16f, 0.08f, 0.12f, TIMBER);
        /* the roof, overhanging a little */
        emit_quad2(&m, v3_(-hx - 0.4f, wh + 0.02f, hz + 0.4f), v3_(hx + 0.4f, wh + 0.02f, hz + 0.4f), v3_(hx + 0.4f, rh + 0.02f, -hz - 0.3f), v3_(-hx - 0.4f, rh + 0.02f, -hz - 0.3f), TIMBER);
        emit_box(&m, v3_(2.2f, rh - 0.2f, -1.8f), AX, AY, AZ, 0.35f, 0.9f, 0.35f, PLINTH);   /* chimney */
        /* plinth band and floor */
        emit_box(&m, v3_(0, 0.18f, 0), AX, AY, AZ, hx + 0.12f, 0.18f, hz + 0.12f, PLINTH);
        emit_box(&m, v3_(0, 0.38f, 0), AX, AY, AZ, hx - 0.1f, 0.02f, hz - 0.1f, FLOOR1);
        /* a bunk against the back wall and a stool */
        emit_box(&m, v3_(-1.9f, 0.7f, -2.2f), AX, AY, AZ, 1.3f, 0.3f, 0.7f, 0x8A6A4AFF);
        emit_box(&m, v3_(-1.9f, 1.05f, -2.2f), AX, AY, AZ, 1.2f, 0.06f, 0.6f, 0x6A5AA0FF);
        emit_prism(&m, v3_(2.4f, 0.4f, 0.8f), 0.3f, 0.0f, 0.45f, TIMBER);
    }
    return m;
}

mesh mesh_house_glow(int style) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(VILLAGE_MESH_CAP, sizeof(vertex));
    if (style == 0) {
        /* a lantern hanging from the top of the dome */
        emit_sphere(&m, v3_(0, 3.4f, 0), 0.22f, 0xFFD890FF);
    } else {
        /* windows in the side walls, lit from within */
        float hx = 3.6f;
        emit_box(&m, v3_(-hx, 2.0f, 0.6f), AX, AY, AZ, 0.02f, 0.45f, 0.45f, 0xFFE0A0FF);
        emit_box(&m, v3_(hx, 2.0f, -0.8f), AX, AY, AZ, 0.02f, 0.45f, 0.45f, 0xFFE0A0FF);
        emit_box(&m, v3_(hx, 2.0f, 1.2f), AX, AY, AZ, 0.02f, 0.45f, 0.45f, 0xFFE0A0FF);
    }
    return m;
}

mesh mesh_alien(int style) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(VILLAGE_MESH_CAP, sizeof(vertex));
    if (style == 0) {
        /* VESK: tall and thin, a long green body, a big pale head, three eyes, dangling arms */
        emit_prism(&m, v3_(0, 0, 0), 0.30f, 0.0f, 1.6f, 0x6CC85AFF);
        emit_prism(&m, v3_(0, 0, 0), 0.34f, 0.55f, 0.75f, 0x4E9A44FF);   /* a belt */
        emit_sphere(&m, v3_(0, 2.05f, 0), 0.48f, 0xA8E890FF);
        emit_box(&m, v3_(-0.42f, 1.0f, 0), AX, AY, AZ, 0.07f, 0.55f, 0.07f, 0x6CC85AFF);
        emit_box(&m, v3_(0.42f, 1.0f, 0), AX, AY, AZ, 0.07f, 0.55f, 0.07f, 0x6CC85AFF);
        emit_box(&m, v3_(-0.16f, -0.0f, 0), AX, AY, AZ, 0.1f, 0.02f, 0.18f, 0x4E9A44FF);   /* feet */
        emit_box(&m, v3_(0.16f, -0.0f, 0), AX, AY, AZ, 0.1f, 0.02f, 0.18f, 0x4E9A44FF);
    } else {
        /* MOMO: short and round, purple, one big eye, an antenna with a bobble, stubby legs */
        emit_sphere(&m, v3_(0, 0.8f, 0), 0.62f, 0x9A5AC8FF);
        emit_box(&m, v3_(-0.25f, 0.12f, 0), AX, AY, AZ, 0.14f, 0.12f, 0.16f, 0x7A42A8FF);
        emit_box(&m, v3_(0.25f, 0.12f, 0), AX, AY, AZ, 0.14f, 0.12f, 0.16f, 0x7A42A8FF);
        emit_box(&m, v3_(0, 1.65f, 0), AX, AY, AZ, 0.04f, 0.28f, 0.04f, 0x7A42A8FF);   /* antenna */
        emit_box(&m, v3_(-0.55f, 0.75f, 0.1f), AX, AY, AZ, 0.12f, 0.08f, 0.2f, 0x7A42A8FF);   /* little arms */
        emit_box(&m, v3_(0.55f, 0.75f, 0.1f), AX, AY, AZ, 0.12f, 0.08f, 0.2f, 0x7A42A8FF);
    }
    return m;
}

mesh mesh_alien_glow(int style) {
    mesh m = { 0, 0, 0, 0 };
    m.v = calloc(VILLAGE_MESH_CAP, sizeof(vertex));
    if (style == 0) {
        /* three dark eyes with bright pupils across the front of the head */
        for (int k = -1; k <= 1; k++) {
            emit_sphere(&m, v3_(k * 0.2f, 2.12f, 0.40f), 0.09f, 0x202030FF);
            emit_sphere(&m, v3_(k * 0.2f, 2.12f, 0.47f), 0.035f, 0xFFF0C0FF);
        }
    } else {
        emit_sphere(&m, v3_(0, 0.92f, 0.50f), 0.24f, 0xF4F4FFFF);   /* one big eye */
        emit_sphere(&m, v3_(0, 0.92f, 0.70f), 0.10f, 0x202030FF);
        emit_sphere(&m, v3_(0, 1.98f, 0), 0.1f, 0xFFA040FF);   /* the bobble */
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
void mesh_put_uv(mesh *m, v3 p, v3 n, uint32_t col, float u, float v) {
    vertex *vx = &m->v[m->count++];
    put(vx, p, n, col);
    vx->uv[0] = u; vx->uv[1] = v;
}

/* A coarser copy of a body for when it is far away: the same heights sampled on a grid div
   times sparser, so it costs a fraction of the triangles. */
static float lod_radius(const void *c, v3 dir, int ring, int seg) {
    const sphere_ctx *s = c; (void)ring; (void)seg;
    return world_surface(s->w, s->i, dir);
}
mesh mesh_planet_lod(const world *w, int i, int div) {
    sphere_ctx c = { w, i, 0, 0 };
    float rep = w->b[i].emissive ? 0 : w->b[i].radius / 4.0f;
    int rings = w->b[i].rings / div, segs = w->b[i].segs / div;
    if (rings < 8) rings = 8;
    if (segs < 12) segs = 12;
    return sphere_grid(rings, segs, rep, lod_radius, planet_color, &c, 1);
}
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
