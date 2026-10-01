#include "moon.h"
#include "noise.h"
#include "fight.h"   /* frandf */
#include <stdlib.h>
#include <string.h>

#define STEP        5.0f    /* tunnel node spacing */
#define MAX_SLOPE   0.53f   /* sin 32 degrees: tunnels stay walkable */
#define TRUNK_R     3.6f
#define BRANCH_R    2.9f
#define BLEND       1.3f    /* how softly caves blend into rock and into each other */
#define WALL_NOISE  0.45f
#define GEM_MAX     80

static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float smin(float a, float b, float k) {
    float h = clamp01(0.5f + 0.5f * (b - a) / k);
    return b + (a - b) * h - k * h * (1 - h);
}
static float capsule(v3 p, v3 a, v3 b) {
    v3 ab = v3_sub(b, a), ap = v3_sub(p, a);
    float t = v3_dot(ap, ab) / v3_dot(ab, ab);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return v3_len(v3_sub(ap, v3_scale(ab, t)));
}

/* ---- the field ---- */

/* How carved the point is: > 0 inside a cave, the largest over every primitive in the list. */
static float carve(const body *b, v3 p, float R, const int *idx, int n) {
    const cavenet *c = &b->caves;
    float air = c->core_r - R;
    for (int k = 0; k < n; k++) {
        int i = idx ? idx[k] : k;
        float in;
        if (i < c->nseg) in = c->seg[i].r - capsule(p, c->seg[i].a, c->seg[i].b);
        else { const cave_blob *bl = &c->blob[i - c->nseg]; in = bl->r - v3_len(v3_sub(p, bl->c)); }
        if (in > air) air = in;
    }
    return air;
}

static float field(const world *w, int bi, v3 p, const int *idx, int n) {
    const body *b = &w->b[bi];
    float R = v3_len(p);
    v3 dir = R > 1e-4f ? v3_scale(p, 1.0f / R) : v3_(0, 1, 0);
    float d = b->radius + world_height(w, bi, dir) - R;   /* the terrain: rock below the surface */
    if (!b->caves.enabled || d < -3.0f) return d;
    float air = carve(b, p, R, idx, n);
    if (air < -6.0f) return d;                            /* nowhere near a cave */
    air += WALL_NOISE * (noise_value(v3_scale(p, 0.33f), b->seed + 77u) - 0.5f) * 2.0f;   /* rough walls */
    return smin(d, -air, BLEND);
}

float moon_field(const world *w, int bi, v3 rel) {
    const cavenet *c = &w->b[bi].caves;
    return field(w, bi, rel, NULL, c->nseg + c->nblob);
}

v3 moon_grad_wide(const world *w, int bi, v3 rel, float e) {
    return v3_((moon_field(w, bi, v3_add(rel, v3_(e, 0, 0))) - moon_field(w, bi, v3_sub(rel, v3_(e, 0, 0)))) / (2 * e),
               (moon_field(w, bi, v3_add(rel, v3_(0, e, 0))) - moon_field(w, bi, v3_sub(rel, v3_(0, e, 0)))) / (2 * e),
               (moon_field(w, bi, v3_add(rel, v3_(0, 0, e))) - moon_field(w, bi, v3_sub(rel, v3_(0, 0, e)))) / (2 * e));
}

v3 moon_grad(const world *w, int bi, v3 rel) { return moon_grad_wide(w, bi, rel, 0.2f); }

float moon_depth(const world *w, int bi, v3 rel) {
    float R = v3_len(rel);
    v3 dir = R > 1e-4f ? v3_scale(rel, 1.0f / R) : v3_(0, 1, 0);
    return world_surface(w, bi, dir) - R;
}

/* ---- carving the network ---- */

static void add_seg(cavenet *c, v3 a, v3 b, float r) {
    if (c->nseg >= CAVE_SEG_MAX) return;
    c->seg[c->nseg].a = a; c->seg[c->nseg].b = b; c->seg[c->nseg].r = r; c->nseg++;
}
/* A cavern, only where it stays well underground. */
static void add_blob(world *w, int bi, v3 p, float r) {
    cavenet *c = &w->b[bi].caves;
    if (c->nblob >= CAVE_BLOB_MAX) return;
    if (moon_depth(w, bi, p) < r + 2.5f) return;
    c->blob[c->nblob].c = p; c->blob[c->nblob].r = r; c->nblob++;
}

/* Branches are queued while the trunks are walked, and walked afterwards, so the trunks always
   get the segment budget they need to reach the core. */
typedef struct { v3 pos, h; float r; int len, depth; } branch_req;
static branch_req queue[64];
static int nqueue;

/* One tunnel: a random walk of STEP-long capsules. Trunks are pulled toward the core and stop
   when they reach it; branches wander, sometimes end in a cavern, and may branch again. */
static void walk(world *w, int bi, uint32_t *s, v3 pos, v3 h, float base_r, int trunk, int max_nodes, int depth, int budget) {
    body *b = &w->b[bi];
    cavenet *c = &b->caves;
    for (int n = 0; n < max_nodes && c->nseg < budget; n++) {
        v3 up = v3_norm(pos);
        v3 jit = v3_(frandf(s, -1, 1), frandf(s, -1, 1), frandf(s, -1, 1));
        float pull = trunk ? 0.30f : 0.06f;
        h = v3_norm(v3_add(v3_add(h, v3_scale(jit, 0.45f)), v3_scale(up, -pull)));
        /* keep the slope walkable, and never let a tunnel climb back to the surface */
        v3 horiz = v3_tangent(h, up);
        float hl = v3_len(horiz);
        if (hl < 1e-3f) { horiz = v3_any_perp(up); hl = 1; }
        horiz = v3_scale(horiz, 1.0f / hl);
        float down = -v3_dot(h, up);
        if (down > MAX_SLOPE) down = MAX_SLOPE;
        if (down < (trunk ? 0.12f : -0.10f)) down = trunk ? 0.12f : -0.10f;
        h = v3_norm(v3_sub(v3_scale(horiz, sqrtf(1 - down * down)), v3_scale(up, down)));
        v3 next = v3_add(pos, v3_scale(h, STEP));
        if (n > 0 && moon_depth(w, bi, next) < base_r + 1.5f) {   /* about to break the surface: dive */
            h = v3_norm(v3_sub(v3_scale(horiz, 0.85f), v3_scale(up, MAX_SLOPE)));
            next = v3_add(pos, v3_scale(h, STEP));
        }
        float r = base_r * frandf(s, 0.85f, 1.15f);
        add_seg(c, pos, next, r);
        pos = next;
        if (v3_len(pos) < c->core_r + 2.0f) return;   /* joined the core */
        if (trunk && n % 9 == 8) add_blob(w, bi, pos, frandf(s, 6.0f, 9.0f));
        if (n % 4 == 3 && depth < 2 && nqueue < 64 && frandf(s, 0, 1) < (trunk ? 0.75f : 0.45f)) {
            add_blob(w, bi, pos, frandf(s, 4.5f, 6.0f));   /* a junction cavern */
            float ang = frandf(s, 1.0f, 1.9f) * (frandf(s, 0, 1) < 0.5f ? 1.0f : -1.0f);
            branch_req *q = &queue[nqueue++];
            q->pos = pos;
            q->h = v3_norm(v3_sub(v3_rotate(horiz, up, ang), v3_scale(up, 0.15f)));
            q->r = base_r * 0.85f; q->len = (int)frandf(s, 6, 13); q->depth = depth + 1;
        }
    }
    if (!trunk && frandf(s, 0, 1) < 0.6f) add_blob(w, bi, pos, frandf(s, 5.0f, 8.0f));   /* a branch ending in a chamber */
}

void moon_setup(world *w, int bi) {
    body *b = &w->b[bi];
    cavenet *c = &b->caves;
    memset(c, 0, sizeof *c);
    if (b->terrain != TERRAIN_MOON) return;
    c->enabled = 1;
    c->core_r = b->radius * 0.30f;
    c->cell = b->radius > 56 ? 2.4f : 2.0f;   /* the big moon is meshed a little coarser */
    uint32_t s = b->seed * 7u + 13u;
    int want = b->radius > 56 ? 3 : 2;
    int budget = (int)(b->radius * 2.4f);
    if (budget > CAVE_SEG_MAX) budget = CAVE_SEG_MAX;
    nqueue = 0;
    for (int m = 0; m < want && c->nmouth < CAVE_MOUTH_MAX; m++) {
        v3 dir = v3_(0, 1, 0);
        for (int tries = 0; tries < 60; tries++) {
            v3 d = v3_norm(v3_(frandf(&s, -1, 1), frandf(&s, -0.6f, 0.6f), frandf(&s, -1, 1)));
            int ok = 1;
            for (int k = 0; k < c->nmouth; k++) if (v3_dot(d, c->mouth[k]) > 0.2f) ok = 0;   /* well apart */
            if (ok) { dir = d; break; }
        }
        c->mouth[c->nmouth++] = dir;
        /* start just outside the ground, heading in and down at 25 degrees, so the carve opens
           a proper mouth in the hillside */
        v3 start = v3_scale(dir, world_surface(w, bi, dir) + 3.0f);
        v3 t = v3_rotate(v3_any_perp(dir), dir, frandf(&s, 0, 6.2831853f));
        v3 h = v3_norm(v3_sub(v3_scale(t, 0.906f), v3_scale(dir, 0.423f)));
        walk(w, bi, &s, start, h, TRUNK_R, 1, 60, 0, CAVE_SEG_MAX);
    }
    /* then the side passages, until the budget runs out */
    for (int q = 0; q < nqueue && c->nseg < budget; q++)
        walk(w, bi, &s, queue[q].pos, queue[q].h, queue[q].r, 0, queue[q].len, queue[q].depth, budget);
}

/* ---- crystals ---- */

typedef struct { v3 pos, n; float s; uint32_t col; } gem;

/* From p, walk along d until the rock is hit; the gem sits on that wall. */
static int gem_on_wall(const world *w, int bi, v3 p, v3 d, gem *g, float size, uint32_t col) {
    d = v3_norm(d);
    for (int k = 0; k < 40; k++) {
        v3 q = v3_add(p, v3_scale(d, 0.5f * k));
        if (moon_field(w, bi, q) > 0) {
            if (k == 0) return 0;
            g->pos = v3_add(p, v3_scale(d, 0.5f * (k - 1)));
            g->n = v3_norm(v3_scale(moon_grad(w, bi, g->pos), -1.0f));   /* out of the wall */
            g->s = size; g->col = col;
            return 1;
        }
    }
    return 0;
}

static int gem_list(const world *w, int bi, gem *out) {
    const body *b = &w->b[bi];
    const cavenet *c = &b->caves;
    uint32_t s = b->seed * 3u + 99u;
    int n = 0;
    for (int i = 0; i < c->nblob && n < GEM_MAX - 12; i++)
        for (int k = 0; k < 3; k++) {
            v3 d = v3_(frandf(&s, -1, 1), frandf(&s, -1, 1), frandf(&s, -1, 1));
            if (gem_on_wall(w, bi, c->blob[i].c, d, &out[n], frandf(&s, 0.5f, 0.9f), (n & 1) ? 0x70E8FFFF : 0xB090FFFF)) n++;
        }
    for (int i = 2; i < c->nseg && n < GEM_MAX - 12; i += 4) {
        v3 mid = v3_scale(v3_add(c->seg[i].a, c->seg[i].b), 0.5f);
        v3 axis = v3_norm(v3_sub(c->seg[i].b, c->seg[i].a));
        v3 d = v3_rotate(v3_any_perp(axis), axis, frandf(&s, 0, 6.2831853f));
        if (gem_on_wall(w, bi, mid, d, &out[n], frandf(&s, 0.3f, 0.5f), (i & 4) ? 0x70E8FFFF : 0xB090FFFF)) n++;
    }
    for (int k = 0; k < 10 && n < GEM_MAX; k++) {   /* around the core wall */
        v3 d = v3_(frandf(&s, -1, 1), frandf(&s, -1, 1), frandf(&s, -1, 1));
        if (gem_on_wall(w, bi, v3_(0, 0, 0), d, &out[n], frandf(&s, 0.9f, 1.5f), (k & 1) ? 0x70E8FFFF : 0xB090FFFF)) n++;
    }
    return n;
}

mesh moon_crystals(const world *w, int bi) {
    const cavenet *c = &w->b[bi].caves;
    mesh m = { 0, 0, 0, 0 };
    if (!c->enabled) return m;
    static gem g[GEM_MAX];
    int n = gem_list(w, bi, g);
    m.v = calloc((size_t)n * 24 + 48, sizeof(vertex));
    for (int i = 0; i < n; i++) {
        v3 up = g[i].n, r = v3_any_perp(up), f = v3_cross(r, up);
        mesh_octa(&m, v3_add(g[i].pos, v3_scale(up, g[i].s * 0.5f)), r, up, f, g[i].s * 0.45f, g[i].s * 1.2f, g[i].s * 0.45f, g[i].col);
    }
    return m;
}

/* ---- meshing: naive surface nets over a voxel grid ---- */

static uint32_t rgb_of(float r, float g, float bl) {
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (bl > 255) bl = 255;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)bl << 8) | 0xFF;
}

/* Surface colour: the moon's own colouring, dimmed and cooled underground, lit by crystals. */
static uint32_t vertex_colour(const world *w, int bi, v3 p, const gem *g, int ng) {
    const body *b = &w->b[bi];
    v3 dir = v3_norm(p);
    uint32_t base = world_color(w, bi, dir, world_height_raw(w, bi, dir));
    float r = (base >> 24) & 0xFF, gg = (base >> 16) & 0xFF, bl = (base >> 8) & 0xFF;
    float depth = moon_depth(w, bi, p);
    if (depth > 0.6f) {
        float k = clamp01((depth - 0.6f) / 9.0f);
        float dim = 1.0f - 0.7f * k;
        r *= dim * 0.85f; gg *= dim * 0.9f; bl *= dim * 1.05f;
        if (v3_len(p) < b->caves.core_r + 4.0f) { r += 30; gg += 22; bl += 10; }   /* the orb's warmth */
    }
    for (int i = 0; i < ng; i++) {
        float d = v3_len(v3_sub(p, g[i].pos)), reach = 5.0f + g[i].s * 5.0f;
        if (d > reach) continue;
        float k = 1 - d / reach; k *= k * 0.8f;
        r += ((g[i].col >> 24) & 0xFF) * k; gg += ((g[i].col >> 16) & 0xFF) * k; bl += ((g[i].col >> 8) & 0xFF) * k;
    }
    return rgb_of(r, gg, bl);
}

typedef struct { v3 p, n; uint32_t col; } snvert;

/* The primitives binned on a coarse grid over the cube [-E, E]^3, so a point only tests the
   ones nearby. */
#define BIN 10.0f
typedef struct { float E; int NB; int *count, *start, *items; } bins;

static void bins_build(const cavenet *c, float E, bins *bn) {
    int NB = (int)(2 * E / BIN) + 1;
    int nprim = c->nseg + c->nblob;
    int *bin_count = calloc((size_t)NB * NB * NB, sizeof(int));
    int *bin_start = calloc((size_t)NB * NB * NB + 1, sizeof(int));
    int *bin_items = NULL;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            bin_start[0] = 0;
            for (int i = 0; i < NB * NB * NB; i++) bin_start[i + 1] = bin_start[i] + bin_count[i];
            bin_items = malloc(sizeof(int) * (size_t)(bin_start[NB * NB * NB] + 1));
            memset(bin_count, 0, sizeof(int) * (size_t)NB * NB * NB);
        }
        for (int i = 0; i < nprim; i++) {
            v3 lo, hi; float r;
            if (i < c->nseg) {
                const cave_seg *sg = &c->seg[i];
                lo = v3_(fminf(sg->a.x, sg->b.x), fminf(sg->a.y, sg->b.y), fminf(sg->a.z, sg->b.z));
                hi = v3_(fmaxf(sg->a.x, sg->b.x), fmaxf(sg->a.y, sg->b.y), fmaxf(sg->a.z, sg->b.z));
                r = sg->r;
            } else { lo = hi = c->blob[i - c->nseg].c; r = c->blob[i - c->nseg].r; }
            r += 6.5f;   /* the carve reaches this far past the radius (blend + noise + "nowhere near") */
            int x0 = (int)((lo.x - r + E) / BIN), x1 = (int)((hi.x + r + E) / BIN);
            int y0 = (int)((lo.y - r + E) / BIN), y1 = (int)((hi.y + r + E) / BIN);
            int z0 = (int)((lo.z - r + E) / BIN), z1 = (int)((hi.z + r + E) / BIN);
            for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < NB; x++)
                for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < NB; y++)
                    for (int z = z0 < 0 ? 0 : z0; z <= z1 && z < NB; z++) {
                        int bidx = (x * NB + y) * NB + z;
                        if (pass == 1) bin_items[bin_start[bidx] + bin_count[bidx]] = i;
                        bin_count[bidx]++;
                    }
        }
    }
    bn->E = E; bn->NB = NB; bn->count = bin_count; bn->start = bin_start; bn->items = bin_items;
}

static void bins_free(bins *bn) { free(bn->count); free(bn->start); free(bn->items); }

/* Exactly moon_field: a primitive left out of p's bin is more than 6.5 past its radius from
   p, and the field ignores every cave that far away. Outside the binned cube it tests them all. */
static float bins_field(const world *w, int bi, const bins *bn, v3 p) {
    float E = bn->E;
    if (p.x + E < 0 || p.y + E < 0 || p.z + E < 0) return moon_field(w, bi, p);
    int bx = (int)((p.x + E) / BIN), by = (int)((p.y + E) / BIN), bz = (int)((p.z + E) / BIN);
    int NB = bn->NB;
    if (bx >= NB || by >= NB || bz >= NB) return moon_field(w, bi, p);
    int bidx = (bx * NB + by) * NB + bz;
    return field(w, bi, p, bn->items + bn->start[bidx], bn->count[bidx]);
}

/* Exactly moon_grad, through the bins. */
static v3 bins_grad(const world *w, int bi, const bins *bn, v3 rel) {
    const float e = 0.2f;
    return v3_((bins_field(w, bi, bn, v3_add(rel, v3_(e, 0, 0))) - bins_field(w, bi, bn, v3_sub(rel, v3_(e, 0, 0)))) / (2 * e),
               (bins_field(w, bi, bn, v3_add(rel, v3_(0, e, 0))) - bins_field(w, bi, bn, v3_sub(rel, v3_(0, e, 0)))) / (2 * e),
               (bins_field(w, bi, bn, v3_add(rel, v3_(0, 0, e))) - bins_field(w, bi, bn, v3_sub(rel, v3_(0, 0, e)))) / (2 * e));
}

void moon_build(const world *w, int bi, mesh out[8]) {
    const body *b = &w->b[bi];
    const cavenet *c = &b->caves;
    for (int o = 0; o < 8; o++) { mesh e = { 0, 0, 0, 0 }; out[o] = e; }
    if (!c->enabled) return;
    float cell = c->cell;
    float E = b->radius + b->amp + 2 * cell;
    int N = (int)(2 * E / cell) + 2;              /* corners per axis */
    float outer = b->radius + b->amp + cell;      /* beyond this is air for sure */
    bins bn;
    bins_build(c, E, &bn);

    /* the field at every corner */
    float *f = malloc(sizeof(float) * (size_t)N * N * N);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            for (int k = 0; k < N; k++) {
                v3 p = v3_(-E + i * cell, -E + j * cell, -E + k * cell);
                float R = v3_len(p);
                float *fp = &f[((size_t)i * N + j) * N + k];
                if (R > outer) { *fp = outer - R; continue; }
                *fp = bins_field(w, bi, &bn, p);
            }

    /* one vertex per cell the surface passes through, at the mean of its edge crossings */
    int NC = N - 1;
    int *cellv = malloc(sizeof(int) * (size_t)NC * NC * NC);
    int cap = 4096, nv = 0;
    snvert *verts = malloc(sizeof(snvert) * (size_t)cap);
    static gem g[GEM_MAX];
    int ng = gem_list(w, bi, g);
    static const int corner[8][3] = { {0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1} };
    static const int edge[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
#define F(i, j, k) f[(((size_t)(i)) * N + (j)) * N + (k)]
    for (int i = 0; i < NC; i++)
        for (int j = 0; j < NC; j++)
            for (int k = 0; k < NC; k++) {
                float v[8];
                int pos = 0, neg = 0;
                for (int q = 0; q < 8; q++) { v[q] = F(i + corner[q][0], j + corner[q][1], k + corner[q][2]); if (v[q] > 0) pos++; else neg++; }
                int *cv = &cellv[((size_t)i * NC + j) * NC + k];
                if (!pos || !neg) { *cv = -1; continue; }
                v3 sum = v3_(0, 0, 0); int cnt = 0;
                for (int e = 0; e < 12; e++) {
                    int a = edge[e][0], bb = edge[e][1];
                    if ((v[a] > 0) == (v[bb] > 0)) continue;
                    float t = v[a] / (v[a] - v[bb]);
                    v3 pa = v3_(i + corner[a][0], j + corner[a][1], k + corner[a][2]);
                    v3 pb = v3_(i + corner[bb][0], j + corner[bb][1], k + corner[bb][2]);
                    sum = v3_add(sum, v3_lerp(pa, pb, t)); cnt++;
                }
                v3 p = v3_add(v3_scale(sum, cell / cnt), v3_(-E, -E, -E));
                if (nv == cap) { cap *= 2; verts = realloc(verts, sizeof(snvert) * (size_t)cap); }
                verts[nv].p = p;
                v3 gr = bins_grad(w, bi, &bn, p);
                verts[nv].n = v3_len(gr) > 1e-5f ? v3_norm(v3_scale(gr, -1.0f)) : v3_norm(p);   /* out of the rock */
                verts[nv].col = vertex_colour(w, bi, p, g, ng);
                *cv = nv++;
            }
    bins_free(&bn);

    /* a quad for every grid edge the surface crosses, joining the four cells around it. Each
       octant gets an indexed mesh: first find which vertices each octant uses and count its
       triangles, then number the vertices per octant and write the indices. */
    uint8_t *used = calloc((size_t)nv, 1);
    int tri_count[8] = { 0 };
    int *oidx = malloc(sizeof(int) * (size_t)nv * 8);
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            for (int o = 0; o < 8; o++) {
                int nvo = 0;
                for (int v = 0; v < nv; v++) if (used[v] & (1 << o)) nvo++;
                out[o].v = calloc((size_t)nvo + 1, sizeof(vertex));
                out[o].idx = malloc(sizeof(uint16_t) * ((size_t)tri_count[o] * 3 + 3));
                out[o].count = 0; out[o].idx_count = 0;
                for (int v = 0; v < nv; v++) {
                    if (!(used[v] & (1 << o))) { oidx[v * 8 + o] = -1; continue; }
                    oidx[v * 8 + o] = out[o].count;
                    /* detail texture: spherical mapping like the planets, with the seam placed
                       on the far side of this octant so no triangle straddles it */
                    v3 dir = v3_norm(verts[v].p);
                    float th = acosf(dir.y > 1 ? 1 : (dir.y < -1 ? -1 : dir.y)), ph = atan2f(dir.z, dir.x);
                    if (o & 4) { if (ph < 0.5f * (float)M_PI) ph += 2 * (float)M_PI; }   /* z < 0: seam at +z */
                    else if (ph < -0.5f * (float)M_PI) ph += 2 * (float)M_PI;            /* z >= 0: seam at -z */
                    float rep = b->radius / 3.0f;
                    mesh_put_uv(&out[o], verts[v].p, verts[v].n, verts[v].col, ph / (2 * (float)M_PI) * 2 * rep, th / (float)M_PI * rep);
                }
            }
        }
        for (int i = 0; i < N; i++)
            for (int j = 0; j < N; j++)
                for (int k = 0; k < N; k++) {
                    float v0 = F(i, j, k);
                    for (int axis = 0; axis < 3; axis++) {
                        int i1 = i + (axis == 0), j1 = j + (axis == 1), k1 = k + (axis == 2);
                        if (i1 >= N || j1 >= N || k1 >= N) continue;
                        float v1 = F(i1, j1, k1);
                        if ((v0 > 0) == (v1 > 0)) continue;
                        /* the four cells sharing this edge */
                        int ci[4], valid = 1;
                        for (int q = 0; q < 4; q++) {
                            int da = q & 1, db = (q >> 1) & 1;   /* offsets in the two other axes */
                            int ci_ = i, cj = j, ck = k;
                            if (axis == 0) { cj -= da; ck -= db; }
                            else if (axis == 1) { ci_ -= da; ck -= db; }
                            else { ci_ -= da; cj -= db; }
                            if (ci_ < 0 || cj < 0 || ck < 0 || ci_ >= NC || cj >= NC || ck >= NC) { valid = 0; break; }
                            ci[q] = cellv[((size_t)ci_ * NC + cj) * NC + ck];
                            if (ci[q] < 0) { valid = 0; break; }
                        }
                        if (!valid) continue;
                        /* order the quad around the edge: (0,0) (1,0) (1,1) (0,1) */
                        int qa = ci[0], qb = ci[1], qc = ci[3], qd = ci[2];
                        v3 centre = v3_scale(v3_add(v3_add(verts[qa].p, verts[qb].p), v3_add(verts[qc].p, verts[qd].p)), 0.25f);
                        int o = (centre.x < 0 ? 1 : 0) | (centre.y < 0 ? 2 : 0) | (centre.z < 0 ? 4 : 0);
                        if (pass == 0) {
                            tri_count[o] += 2;
                            used[qa] |= 1 << o; used[qb] |= 1 << o; used[qc] |= 1 << o; used[qd] |= 1 << o;
                            continue;
                        }
                        /* wind each triangle so its face points out of the rock. The edge's
                           corner values say which way that is (the air is on the negative
                           side), which is reliable even where the gradient is not */
                        v3 want = v3_(axis == 0, axis == 1, axis == 2);
                        if (v0 > 0) want = v3_scale(want, -1.0f);   /* rock at the low corner: air is toward -axis... */
                        want = v3_scale(want, -1.0f);               /* ...and the face must point into the air */
                        int tris[2][3] = { { qa, qb, qc }, { qa, qc, qd } };
                        for (int t = 0; t < 2; t++) {
                            int a = tris[t][0], bq = tris[t][1], cq = tris[t][2];
                            v3 face = v3_cross(v3_sub(verts[bq].p, verts[a].p), v3_sub(verts[cq].p, verts[a].p));
                            if (v3_dot(face, want) < 0) { int tmp = bq; bq = cq; cq = tmp; }
                            uint16_t *ix = &out[o].idx[out[o].idx_count];
                            ix[0] = (uint16_t)oidx[a * 8 + o]; ix[1] = (uint16_t)oidx[bq * 8 + o]; ix[2] = (uint16_t)oidx[cq * 8 + o];
                            out[o].idx_count += 3;
                        }
                    }
                }
    }
    free(used); free(oidx);
#undef F
    free(f); free(cellv); free(verts);
}
