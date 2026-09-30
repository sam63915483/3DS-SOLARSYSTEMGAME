/* Cave meshes, in body space, every wall facing inward. The tunnel is a swept tube along the
   sampled spline with rough walls, a flat floor and smooth normals; its first ring is clipped to
   exactly where the tube meets the terrain and skirted with a strip that hugs the terrain, so
   the mouth has no cracks; its last ring is clipped and skirted the same way onto the cavern
   wall. Crystal light is baked into the vertex colours. */
#include "mesh.h"
#include "cave.h"
#include "noise.h"
#include <stdlib.h>

#define SIDES 14
#define SKIRT_STEPS 3
#define SKIRT_W 1.6f      /* per step: 4.8 wide in all, wider than a terrain cell */
#define LIFT 0.06f
#define ROUGH 0.12f
#define ROCK_A 0x5E5866FF
#define ROCK_B 0x4A4452FF
#define CAVERN_A 0x2E2A44FF
#define CAVERN_B 0x262238FF
#define SHELL 0x100E16FF
#define CRYSTAL_A 0x70E8FFFF
#define CRYSTAL_B 0xB090FFFF
#define ORB 0xFFD080FF
#define MAX_CRYSTALS 64

/* ---- crystals: placed once, used for the glow bake and the crystal mesh ---- */

typedef struct { v3 pos, in, r, f; float s; uint32_t col; } crystal;

static int crystal_list(const world *w, int bi, crystal *out) {
    const body *b = &w->b[bi];
    const cave *c = &b->cave;
    int n = 0;
    static v3 u[CAVE_SAMP_MAX], t1[CAVE_SAMP_MAX], t2[CAVE_SAMP_MAX];
    static float down[CAVE_SAMP_MAX];
    cave_frames(c, u, t1, t2, down);
    /* one every six samples along the tunnel, on the upper walls, never on the floor */
    for (int k = 6; k + 3 < c->nsamp && n < MAX_CRYSTALS - 16; k += 6) {
        float a = down[k] + (float)M_PI + (noise_hashf(k, 1, 8, b->seed) - 0.5f) * 3.4f;
        v3 dir = v3_add(v3_scale(t1[k], cosf(a)), v3_scale(t2[k], sinf(a)));
        crystal *q = &out[n++];
        q->pos = v3_add(c->samp[k], v3_scale(dir, c->tube_r * 0.86f));
        q->in = v3_scale(dir, -1.0f);
        q->r = v3_norm(v3_cross(q->in, u[k])); q->f = v3_cross(q->r, q->in);
        q->s = 0.25f + noise_hashf(k, 2, 8, b->seed) * 0.3f;
        q->col = (k / 6) & 1 ? CRYSTAL_A : CRYSTAL_B;
    }
    /* a scatter of bigger ones around the cavern wall, away from where the tunnel comes in */
    v3 entry = v3_norm(c->samp[c->nsamp - 1]);
    for (int g = 0; g < 16 && n < MAX_CRYSTALS; g++) {
        v3 d = v3_norm(v3_(noise_hashf(g, 2, 8, b->seed) * 2 - 1, noise_hashf(g, 3, 8, b->seed) * 2 - 1, noise_hashf(g, 4, 8, b->seed) * 2 - 1));
        if (cave_angle(d, entry) < 0.7f) d = v3_scale(d, -1.0f);
        crystal *q = &out[n++];
        q->pos = v3_scale(d, c->core_r * 0.93f);
        q->in = v3_scale(d, -1.0f);
        q->r = v3_any_perp(q->in); q->f = v3_cross(q->r, q->in);
        q->s = 0.6f + noise_hashf(g, 5, 8, b->seed) * 0.9f;
        q->col = g & 1 ? CRYSTAL_A : CRYSTAL_B;
    }
    return n;
}

static uint32_t rgb_of(float r, float g, float bl) {
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (bl > 255) bl = 255;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)bl << 8) | 0xFF;
}

/* Rock colour at a point: two-tone by noise, darker with depth, lit by nearby crystals. */
static uint32_t rock_colour(const body *b, v3 p, const crystal *cr, int ncr, uint32_t ca, uint32_t cb, float bright) {
    float t = noise_value(v3_scale(p, 0.35f), b->seed + 5u);
    float r = ((ca >> 24) & 0xFF) * (1 - t) + ((cb >> 24) & 0xFF) * t;
    float g = ((ca >> 16) & 0xFF) * (1 - t) + ((cb >> 16) & 0xFF) * t;
    float bl = ((ca >> 8) & 0xFF) * (1 - t) + ((cb >> 8) & 0xFF) * t;
    r *= bright; g *= bright; bl *= bright;
    for (int i = 0; i < ncr; i++) {
        float d = v3_len(v3_sub(p, cr[i].pos)), reach = 6.0f + cr[i].s * 6.0f;
        if (d > reach) continue;
        float k = (1 - d / reach); k *= k * 0.9f;
        r += ((cr[i].col >> 24) & 0xFF) * k; g += ((cr[i].col >> 16) & 0xFF) * k; bl += ((cr[i].col >> 8) & 0xFF) * k;
    }
    return rgb_of(r, g, bl);
}

/* ---- the tunnel ---- */

typedef struct {
    const world *w; int bi;
    v3 u[CAVE_SAMP_MAX], t1[CAVE_SAMP_MAX], t2[CAVE_SAMP_MAX]; float down[CAVE_SAMP_MAX];
} frames;

/* Rough tube point on ring k at side j (or a fractional s along the sampled axis for clipping). */
static v3 tube_point(const cave *c, const frames *f, float s, int j, uint32_t seed) {
    int k = (int)s; if (k > c->nsamp - 2) k = c->nsamp - 2; if (k < 0) k = 0;
    float t = s - k; if (t < 0) t = 0; if (t > 1) t = 1;
    v3 axis = v3_lerp(c->samp[k], c->samp[k + 1], t);
    v3 t1 = v3_norm(v3_lerp(f->t1[k], f->t1[k + 1], t)), t2 = v3_norm(v3_lerp(f->t2[k], f->t2[k + 1], t));
    float dn = f->down[k];
    float a = 6.2831853f * j / SIDES;
    float rad = cave_wall_radius(c, a, dn);
    float rough = 1.0f + ROUGH * (noise_value(v3_(s * 0.45f, j * 0.9f, 3.1f), seed) - 0.5f) * 2.0f;
    if (rad < c->tube_r - 1e-3f) rough = 1.0f + (rough - 1.0f) * 0.25f;   /* the floor stays walkable */
    return v3_add(axis, v3_scale(v3_add(v3_scale(t1, cosf(a)), v3_scale(t2, sinf(a))), rad * rough));
}

static float terrain_r(const world *w, int bi, v3 p) { return world_surface(w, bi, v3_norm(p)); }
static float cavern_r(const world *w, int bi, v3 dir) {
    const cave *c = &w->b[bi].cave;
    return c->core_r * (1.0f + 0.10f * (noise_fbm(v3_scale(dir, 2.5f), 2, w->b[bi].seed + 9u) - 0.5f) * 2.0f);
}

/* For side j, the s at which the tube surface crosses the terrain (the mouth). Vertices before
   it are in the air and get clamped to the crossing. */
static float mouth_cross(const cave *c, const frames *f, int j, uint32_t seed) {
    float lo = 0, hi = 0;
    int found = 0;
    for (int k = 1; k < c->nsamp && k < 40; k++) {
        v3 p = tube_point(c, f, (float)k, j, seed);
        if (v3_len(p) < terrain_r(f->w, f->bi, p)) { hi = (float)k; lo = (float)(k - 1); found = 1; break; }
    }
    if (!found) return 0;
    for (int it = 0; it < 12; it++) {
        float mid = 0.5f * (lo + hi);
        v3 p = tube_point(c, f, mid, j, seed);
        if (v3_len(p) < terrain_r(f->w, f->bi, p)) hi = mid; else lo = mid;
    }
    return hi;
}

/* For side j, the s at which the tube surface enters the cavern. Vertices past it get clamped. */
static float cavern_cross(const cave *c, const frames *f, int j, uint32_t seed) {
    float lo = (float)(c->nsamp - 2), hi = lo;
    int found = 0;
    for (int k = c->nsamp - 2; k >= 0; k--) {
        v3 p = tube_point(c, f, (float)k, j, seed);
        if (v3_len(p) > cavern_r(f->w, f->bi, v3_norm(p))) { lo = (float)k; hi = (float)(k + 1); found = 1; break; }
    }
    if (!found) return (float)(c->nsamp - 2);
    for (int it = 0; it < 12; it++) {
        float mid = 0.5f * (lo + hi);
        v3 p = tube_point(c, f, mid, j, seed);
        if (v3_len(p) > cavern_r(f->w, f->bi, v3_norm(p))) lo = mid; else hi = mid;
    }
    return lo;
}

mesh mesh_cave_tunnel(const world *w, int bi) {
    const body *b = &w->b[bi];
    const cave *c = &b->cave;
    mesh m = { 0, 0, 0, 0 };
    if (!c->enabled || c->nsamp < 3) return m;
    static frames f;
    f.w = w; f.bi = bi;
    cave_frames(c, f.u, f.t1, f.t2, f.down);
    static crystal cr[MAX_CRYSTALS];
    int ncr = crystal_list(w, bi, cr);
    uint32_t seed = b->seed * 31u + 7u;

    int nr = c->nsamp - 1;   /* rings */
    m.v = calloc((size_t)nr * SIDES * 6 + SIDES * SKIRT_STEPS * 12 + 64, sizeof(vertex));
    /* positions: clipped to the terrain at the mouth and the cavern at the far end */
    static v3 P[CAVE_SAMP_MAX][SIDES], N[CAVE_SAMP_MAX][SIDES];
    static uint32_t C[CAVE_SAMP_MAX][SIDES];
    static float s_in[SIDES], s_out[SIDES];
    for (int j = 0; j < SIDES; j++) { s_in[j] = mouth_cross(c, &f, j, seed); s_out[j] = cavern_cross(c, &f, j, seed); }
    for (int k = 0; k < nr; k++)
        for (int j = 0; j < SIDES; j++) {
            float s = (float)k;
            if (s < s_in[j]) s = s_in[j];
            if (s > s_out[j]) s = s_out[j];
            P[k][j] = tube_point(c, &f, s, j, seed);
        }
    /* smooth normals from the neighbours, pointing into the tunnel */
    for (int k = 0; k < nr; k++)
        for (int j = 0; j < SIDES; j++) {
            v3 da = v3_sub(P[k][(j + 1) % SIDES], P[k][(j + SIDES - 1) % SIDES]);
            v3 ds = v3_sub(P[k < nr - 1 ? k + 1 : k][j], P[k > 0 ? k - 1 : k][j]);
            v3 n = v3_cross(da, ds);
            if (v3_len(n) < 1e-6f) n = v3_sub(c->samp[k], P[k][j]);
            n = v3_norm(n);
            if (v3_dot(n, v3_sub(c->samp[k], P[k][j])) < 0) n = v3_scale(n, -1.0f);
            N[k][j] = n;
            float depth = (b->radius - v3_len(P[k][j])) / (b->radius - c->core_r);
            if (depth < 0) depth = 0;
            if (depth > 1) depth = 1;
            float bright = 1.05f - 0.55f * depth;
            if (k < 10) bright += 0.25f * (1.0f - k / 10.0f);   /* daylight at the mouth */
            C[k][j] = rock_colour(b, P[k][j], cr, ncr, ROCK_A, ROCK_B, bright);
        }
    /* walls */
    for (int k = 0; k + 1 < nr; k++)
        for (int j = 0; j < SIDES; j++) {
            int j1 = (j + 1) % SIDES;
            mesh_tri_n(&m, P[k][j], N[k][j], P[k][j1], N[k][j1], P[k + 1][j1], N[k + 1][j1], C[k][j], C[k][j1], C[k + 1][j1]);
            mesh_tri_n(&m, P[k][j], N[k][j], P[k + 1][j1], N[k + 1][j1], P[k + 1][j], N[k + 1][j], C[k][j], C[k + 1][j1], C[k + 1][j]);
        }
    /* skirts: strips out from the clipped rings that hug the terrain (mouth) and the cavern
       wall (far end), lifted a hair off them so they never fight the surface underneath */
    for (int end = 0; end < 2; end++) {
        v3 ring[SIDES], skirt[SKIRT_STEPS + 1][SIDES];
        for (int j = 0; j < SIDES; j++) {
            float s = end ? s_out[j] : s_in[j];
            int k = (int)s; if (k > c->nsamp - 2) k = c->nsamp - 2;
            ring[j] = tube_point(c, &f, s, j, seed);
            v3 axis = v3_lerp(c->samp[k], c->samp[k + 1], s - k);
            v3 outward = v3_norm(v3_sub(ring[j], axis));
            skirt[0][j] = ring[j];
            for (int q = 1; q <= SKIRT_STEPS; q++) {
                v3 p = v3_add(ring[j], v3_scale(outward, SKIRT_W * q));
                v3 dir = v3_norm(p);
                if (!end) p = v3_scale(dir, terrain_r(w, bi, p) + LIFT);
                else p = v3_scale(dir, cavern_r(w, bi, dir) - LIFT);
                skirt[q][j] = p;
            }
        }
        for (int q = 0; q < SKIRT_STEPS; q++)
            for (int j = 0; j < SIDES; j++) {
                int j1 = (j + 1) % SIDES;
                v3 a = skirt[q][j], bb = skirt[q][j1], cc = skirt[q + 1][j1], d = skirt[q + 1][j];
                v3 na = v3_norm(a), nb = v3_norm(bb), nc = v3_norm(cc), nd = v3_norm(d);
                if (end) { na = v3_scale(na, -1); nb = v3_scale(nb, -1); nc = v3_scale(nc, -1); nd = v3_scale(nd, -1); }
                uint32_t ca = end ? CAVERN_A : ROCK_A, cb = end ? CAVERN_B : ROCK_B;
                float br = end ? 0.6f : 1.15f;
                uint32_t col_a = rock_colour(b, a, cr, ncr, ca, cb, br), col_b = rock_colour(b, bb, cr, ncr, ca, cb, br);
                uint32_t col_c = rock_colour(b, cc, cr, ncr, ca, cb, br), col_d = rock_colour(b, d, cr, ncr, ca, cb, br);
                mesh_tri_n(&m, a, na, bb, nb, cc, nc, col_a, col_b, col_c);
                mesh_tri_n(&m, a, na, cc, nc, d, nd, col_a, col_c, col_d);
            }
    }
    return m;
}

/* ---- the cavern: a lumpy hollow sphere with a hole where the tunnel comes in ---- */

mesh mesh_cave_cavern(const world *w, int bi) {
    const body *b = &w->b[bi];
    const cave *c = &b->cave;
    mesh m = { 0, 0, 0, 0 };
    if (!c->enabled) return m;
    static crystal cr[MAX_CRYSTALS];
    int ncr = crystal_list(w, bi, cr);
    const int rings = 16, segs = 24;
    m.v = calloc((size_t)rings * segs * 6, sizeof(vertex));
    static v3 P[17][24], N[17][24];
    static uint32_t C[17][24];
    for (int i = 0; i <= rings; i++)
        for (int j = 0; j < segs; j++) {
            float th = (float)M_PI * i / rings, ph = 2.0f * (float)M_PI * j / segs;
            v3 dir = v3_(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
            P[i][j] = v3_scale(dir, cavern_r(w, bi, dir));
        }
    for (int i = 0; i <= rings; i++)
        for (int j = 0; j < segs; j++) {
            v3 da = v3_sub(P[i][(j + 1) % segs], P[i][(j + segs - 1) % segs]);
            v3 ds = v3_sub(P[i < rings ? i + 1 : i][j], P[i > 0 ? i - 1 : i][j]);
            v3 n = v3_cross(da, ds);
            if (v3_len(n) < 1e-6f || i == 0 || i == rings) n = v3_scale(P[i][j], -1.0f);
            n = v3_norm(n);
            if (v3_dot(n, P[i][j]) > 0) n = v3_scale(n, -1.0f);
            N[i][j] = n;
            C[i][j] = rock_colour(b, P[i][j], cr, ncr, CAVERN_A, CAVERN_B, 0.9f);
        }
    v3 entry = v3_norm(c->samp[c->nsamp - 1]);
    float open = asinf(c->tube_r / c->core_r) * 1.25f;
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            int j1 = (j + 1) % segs;
            v3 centre = v3_norm(v3_add(v3_add(P[i][j], P[i + 1][j]), v3_add(P[i][j1], P[i + 1][j1])));
            if (cave_angle(centre, entry) < open) continue;
            if (i < rings - 1) mesh_tri_n(&m, P[i][j], N[i][j], P[i + 1][j1], N[i + 1][j1], P[i + 1][j], N[i + 1][j], C[i][j], C[i + 1][j1], C[i + 1][j]);
            if (i > 0) mesh_tri_n(&m, P[i][j], N[i][j], P[i][j1], N[i][j1], P[i + 1][j1], N[i + 1][j1], C[i][j], C[i][j1], C[i + 1][j1]);
        }
    return m;
}

mesh mesh_cave_crystals(const world *w, int bi) {
    const cave *c = &w->b[bi].cave;
    mesh m = { 0, 0, 0, 0 };
    if (!c->enabled) return m;
    static crystal cr[MAX_CRYSTALS];
    int ncr = crystal_list(w, bi, cr);
    m.v = calloc((size_t)ncr * 24 + 24 * 2, sizeof(vertex));
    for (int i = 0; i < ncr; i++)
        mesh_octa(&m, cr[i].pos, cr[i].r, cr[i].in, cr[i].f, cr[i].s * 0.6f, cr[i].s * 1.4f, cr[i].s * 0.6f, cr[i].col);
    /* the orb at the heart of the cavern: two nested gems */
    float o = c->core_r * 0.10f;
    mesh_octa(&m, v3_(0, 0, 0), v3_(1, 0, 0), v3_(0, 1, 0), v3_(0, 0, 1), o, o, o, ORB);
    mesh_octa(&m, v3_(0, 0, 0), v3_norm(v3_(1, 1, 0)), v3_norm(v3_(-1, 1, 0)), v3_(0, 0, 1), o * 0.9f, o * 0.9f, o * 1.3f, 0xFFF0C0FF);
    return m;
}

/* ---- the shell: a dark lining under the terrain so a crack never shows stars ---- */

mesh mesh_cave_shell(const world *w, int bi) {
    const cave *c = &w->b[bi].cave;
    mesh m = { 0, 0, 0, 0 };
    if (!c->enabled) return m;
    const int rings = 12, segs = 18;
    m.v = calloc((size_t)rings * segs * 6, sizeof(vertex));
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            v3 p[4];
            for (int q = 0; q < 4; q++) {
                int ii = i + (q == 1 || q == 2), jj = (j + (q >= 2)) % segs;
                float th = (float)M_PI * ii / rings, ph = 2.0f * (float)M_PI * jj / segs;
                p[q] = v3_scale(v3_(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph)), c->under_r);
            }
            v3 centre = v3_scale(v3_add(v3_add(p[0], p[1]), v3_add(p[2], p[3])), 0.25f);
            if (cave_axis_dist(c, centre, NULL) < c->tube_r + 4.0f) continue;   /* the tunnel passes here */
            v3 out = v3_scale(centre, 2.0f);
            if (i < rings - 1) mesh_tri(&m, p[0], p[2], p[1], out, SHELL);
            if (i > 0) mesh_tri(&m, p[0], p[3], p[2], out, SHELL);
        }
    return m;
}
