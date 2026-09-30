#include "cave.h"
#include "noise.h"
#include <string.h>

#define TUBE_R      3.0f
#define FLOOR_K     0.72f   /* floor this fraction of the radius below the axis: 4.2 wide */
#define PIT_R      14.0f    /* units */
#define PIT_DEPTH   7.0f
#define HELIX_R    10.4f    /* horizontal radius of the corkscrew: 3.5 tube radii, so it never folds */
#define STEP_DOWN   2.4f    /* radial drop per bend after that */
#define STEP_ALONG  6.5f    /* travel per bend */
#define SAMPLE_LEN  1.2f

/* Catmull-Rom through the control points; t in 0..npath-1. */
static v3 spline_at(const cave *c, float t) {
    int n = c->npath;
    int i = (int)t;
    if (i >= n - 1) return c->path[n - 1];
    if (i < 0) i = 0;
    float u = t - i;
    v3 p0 = c->path[i > 0 ? i - 1 : 0], p1 = c->path[i], p2 = c->path[i + 1], p3 = c->path[i + 2 < n ? i + 2 : n - 1];
    float u2 = u * u, u3 = u2 * u;
    v3 a = v3_add(v3_scale(p1, 2.0f), v3_(0, 0, 0));
    v3 b = v3_sub(p2, p0);
    v3 cc = v3_sub(v3_add(v3_scale(p0, 2.0f), v3_scale(p2, 4.0f)), v3_add(v3_scale(p1, 5.0f), p3));
    v3 d = v3_add(v3_sub(v3_scale(p1, 3.0f), p0), v3_sub(p3, v3_scale(p2, 3.0f)));
    return v3_scale(v3_add(v3_add(a, v3_scale(b, u)), v3_add(v3_scale(cc, u2), v3_scale(d, u3))), 0.5f);
}

void cave_setup(world *w, int bi) {
    body *b = &w->b[bi];
    cave *c = &b->cave;
    memset(c, 0, sizeof *c);
    if (b->terrain != TERRAIN_MOON) return;
    c->enabled = 1;
    c->entrance = v3_norm(v3_(noise_hashf(1, 2, 3, b->seed) * 2 - 1, (noise_hashf(4, 5, 6, b->seed) - 0.5f) * 0.8f, noise_hashf(7, 8, 9, b->seed) * 2 - 1));
    c->tube_r = TUBE_R;
    c->floor_k = FLOOR_K;
    c->pit_ang = PIT_R / b->radius;
    c->pit_depth = PIT_DEPTH;
    c->core_r = b->radius * 0.30f;
    c->under_r = b->radius - PIT_DEPTH - 3.0f;
    if (c->under_r < c->core_r + 6.0f) c->under_r = c->core_r + 6.0f;

    /* one helix the whole way: a constant horizontal radius HELIX_R about the pit axis (wide
       enough that the tube never folds on itself), dropping STEP_DOWN per STEP_ALONG of travel.
       The mouth is where the helix meets the crater floor near the crater wall, heading
       tangentially, so the roof closes over you as the floor slopes away. */
    v3 e = c->entrance;
    v3 t1 = v3_any_perp(e), t2 = v3_cross(e, t1);
    float r = b->radius;
    for (int it = 0; it < 3; it++) {   /* the mouth's radius depends on the terrain height there */
        v3 dir = v3_norm(v3_add(e, v3_scale(t1, HELIX_R / r)));
        r = b->radius + world_height_raw(w, bi, dir) + FLOOR_K * TUBE_R;
    }
    int n = 0;
    float th = 0;
    for (;;) {
        v3 dir = v3_norm(v3_add(e, v3_scale(v3_add(v3_scale(t1, cosf(th)), v3_scale(t2, sinf(th))), HELIX_R / r)));
        c->path[n++] = v3_scale(dir, r);
        if (r < c->core_r * 0.8f || n >= CAVE_PATH_MAX) break;
        th += STEP_ALONG / HELIX_R;
        r -= STEP_DOWN;
    }
    c->npath = n;

    /* sample the spline every SAMPLE_LEN */
    int ns = 0;
    v3 prev = spline_at(c, 0);
    c->samp[ns++] = prev;
    float acc = 0;
    for (float t = 0; t < n - 1 && ns < CAVE_SAMP_MAX; t += 0.02f) {
        v3 q = spline_at(c, t);
        acc += v3_len(v3_sub(q, prev));
        prev = q;
        if (acc >= SAMPLE_LEN) { c->samp[ns++] = q; acc = 0; }
    }
    if (ns < CAVE_SAMP_MAX) c->samp[ns++] = c->path[n - 1];
    c->nsamp = ns;
}

void cave_frames(const cave *c, v3 *u, v3 *t1, v3 *t2, float *down_ang) {
    for (int k = 0; k < c->nsamp; k++) {
        int a = k < c->nsamp - 1 ? k : k - 1;
        u[k] = v3_norm(v3_sub(c->samp[a + 1], c->samp[a]));
        v3 prev = k ? t1[k - 1] : v3_any_perp(u[0]);
        v3 p = v3_tangent(prev, u[k]);
        t1[k] = v3_len(p) < 1e-3f ? v3_any_perp(u[k]) : v3_norm(p);
        t2[k] = v3_cross(u[k], t1[k]);
        v3 d = v3_tangent(v3_scale(v3_norm(c->samp[k]), -1.0f), u[k]);
        down_ang[k] = atan2f(v3_dot(d, t2[k]), v3_dot(d, t1[k]));
    }
}

float cave_wall_radius(const cave *c, float a, float down_ang) {
    float rel = a - down_ang;
    while (rel > (float)M_PI) rel -= 2 * (float)M_PI;
    while (rel < -(float)M_PI) rel += 2 * (float)M_PI;
    float lim = acosf(c->floor_k);
    if (fabsf(rel) < lim) return c->tube_r * c->floor_k / cosf(rel);   /* the flat floor chord */
    return c->tube_r;
}

/* Closest sampled segment to rel, with the perpendicular from the axis, the axis direction and
   the local down (perpendicular to the axis). Points before the mouth are outside. */
static int closest(const cave *c, v3 rel, v3 *perp, v3 *u, v3 *down) {
    if (c->nsamp < 2) return -1;
    v3 u0 = v3_norm(v3_sub(c->samp[1], c->samp[0]));
    if (v3_dot(v3_sub(rel, c->samp[0]), u0) < 0) return -1;   /* the open mouth */
    int best = -1; float bd = 1e9f;
    for (int s = 0; s + 1 < c->nsamp; s++) {
        v3 a = c->samp[s], d = v3_sub(c->samp[s + 1], a);
        float l2 = v3_dot(d, d);
        float t = l2 > 0 ? v3_dot(v3_sub(rel, a), d) / l2 : 0;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        v3 ap = v3_add(a, v3_scale(d, t));
        v3 q = v3_sub(rel, ap);
        float dist = v3_len(q);
        if (dist < bd) {
            bd = dist; best = s; *perp = q; *u = v3_norm(d);
            v3 dn = v3_tangent(v3_scale(v3_norm(ap), -1.0f), *u);
            *down = v3_len(dn) > 1e-4f ? v3_norm(dn) : v3_any_perp(*u);
        }
    }
    return best;
}

float cave_axis_dist(const cave *c, v3 rel, int *nearest) {
    float bd = 1e9f; int bk = -1;
    for (int s = 0; s + 1 < c->nsamp; s++) {
        v3 a = c->samp[s], d = v3_sub(c->samp[s + 1], a);
        float l2 = v3_dot(d, d);
        float t = l2 > 0 ? v3_dot(v3_sub(rel, a), d) / l2 : 0;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float dist = v3_len(v3_sub(rel, v3_add(a, v3_scale(d, t))));
        if (dist < bd) { bd = dist; bk = s; }
    }
    if (nearest) *nearest = bk;
    return bd;
}

int cave_inside(const cave *c, v3 rel, float margin, v3 *n, float *pen) {
    *pen = 0; *n = v3_(0, 1, 0);
    if (!c->enabled) return 0;
    v3 perp, u, down;
    int seg = closest(c, rel, &perp, &u, &down);
    if (seg >= 0) {
        float d = v3_len(perp);
        float depth = v3_dot(perp, down);            /* how far below the axis */
        float floor_d = c->tube_r * c->floor_k;
        if (d < c->tube_r && depth < floor_d) {
            /* pushed by whichever is nearer: the round wall or the flat floor */
            float wall_gap = c->tube_r - d, floor_gap = floor_d - depth;
            if (floor_gap < wall_gap) {
                if (floor_gap < margin) { *pen = margin - floor_gap; *n = v3_scale(down, -1.0f); }
            } else if (wall_gap < margin) {
                *pen = margin - wall_gap; *n = d > 1e-4f ? v3_scale(perp, -1.0f / d) : v3_(0, 1, 0);
            }
            return 1;
        }
    }
    float rr = v3_len(rel);
    if (rr < c->core_r) {
        float gap = c->core_r - rr;
        if (gap < margin) { *pen = margin - gap; *n = rr > 1e-4f ? v3_scale(rel, -1.0f / rr) : v3_(0, 1, 0); }
        return 2;
    }
    return 0;
}

float cave_floor_below(const cave *c, v3 rel, v3 up, float max_s) {
    if (!c->enabled) return -1;
    v3 perp, u, down;
    int seg = closest(c, rel, &perp, &u, &down);
    if (seg < 0 || v3_len(perp) >= c->tube_r) return -1;
    v3 w = v3_tangent(up, u);   /* how the perpendicular changes per unit of descent */
    float best = -1;
    /* the flat floor: depth(s) = depth0 + s * (-w . down) */
    float rate = -v3_dot(w, down);
    if (rate > 1e-4f) {
        float s = (c->tube_r * c->floor_k - v3_dot(perp, down)) / rate;
        if (s >= 0 && s <= max_s) best = s;
    }
    /* the round wall */
    float A = v3_dot(w, w), B = -2.0f * v3_dot(perp, w), C = v3_dot(perp, perp) - c->tube_r * c->tube_r;
    if (A > 1e-4f) {
        float disc = B * B - 4 * A * C;
        if (disc >= 0) {
            float s = (-B + sqrtf(disc)) / (2 * A);
            if (s >= 0 && s <= max_s && (best < 0 || s < best)) best = s;
        }
    }
    return best;
}
