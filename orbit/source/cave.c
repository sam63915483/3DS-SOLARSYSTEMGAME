#include "cave.h"
#include "noise.h"
#include <string.h>

#define TUBE_R      2.4f
#define PIT_R       9.0f    /* units */
#define PIT_DEPTH   7.0f
#define SHAFT_UP    0.7f    /* shaft top above the pit floor */
#define SHAFT_DOWN  3.0f    /* shaft bottom below the pit floor */
#define STEP_DOWN   2.4f    /* radial drop per tunnel bend */
#define STEP_ALONG  6.0f    /* travel per bend */
#define HELIX_ANG   0.35f   /* radians the helix swings off the entrance axis */

void cave_setup(world *w, int bi) {
    body *b = &w->b[bi];
    cave *c = &b->cave;
    memset(c, 0, sizeof *c);
    if (b->terrain != TERRAIN_MOON) return;
    c->enabled = 1;
    /* a fixed mouth per moon, off the poles */
    c->entrance = v3_norm(v3_(noise_hashf(1, 2, 3, b->seed) * 2 - 1, (noise_hashf(4, 5, 6, b->seed) - 0.5f) * 0.8f, noise_hashf(7, 8, 9, b->seed) * 2 - 1));
    c->tube_r = TUBE_R;
    c->pit_ang = PIT_R / b->radius;
    c->pit_depth = PIT_DEPTH;
    /* the hole must span a couple of terrain cells or the mesh keeps a lid on it */
    c->hole_ang = 1.6f * (float)M_PI / b->rings;
    if (c->hole_ang < TUBE_R / b->radius) c->hole_ang = TUBE_R / b->radius;
    c->core_r = b->radius * 0.30f;

    /* the pit is already in world_height_raw once entrance/pit_* are set */
    v3 e = c->entrance;
    float floor_r = b->radius + world_height_raw(w, bi, e);
    float shaft_r = c->hole_ang * b->radius * 1.05f;
    v3 t1 = v3_any_perp(e), t2 = v3_cross(e, t1);
    int n = 0;
    /* a wide vertical shaft under the hole that funnels into the tunnel */
    c->radius[n] = shaft_r; c->path[n++] = v3_scale(e, floor_r + SHAFT_UP);
    c->radius[n] = shaft_r; c->path[n++] = v3_scale(e, floor_r - SHAFT_DOWN);
    /* helix: swings straight off the axis, then corkscrews down at a walkable slope; the bend
       angle grows as the radius shrinks so each bend covers about the same distance */
    float r = floor_r - SHAFT_DOWN, th = 0;
    v3 dir = e;
    for (;;) {
        r -= STEP_DOWN;
        if (r < c->core_r * 0.7f || n >= CAVE_PATH_MAX) break;
        dir = v3_norm(v3_add(e, v3_scale(v3_add(v3_scale(t1, cosf(th)), v3_scale(t2, sinf(th))), HELIX_ANG)));
        c->radius[n] = TUBE_R;
        c->path[n++] = v3_scale(dir, r);
        th += STEP_ALONG / (r * HELIX_ANG);
    }
    c->npath = n;
}

/* Closest tunnel segment to rel: returns its index or -1, with the perpendicular from the axis,
   its length, the axis direction and the tunnel radius there. Points before the top of the
   shaft are outside (open mouth). */
static int closest_segment(const cave *c, v3 rel, v3 *perp, float *dist, v3 *axis_dir, float *radius) {
    int best = -1; float bd = 1e9f;
    /* above the top of the shaft is open sky, whatever the segment caps below might say */
    if (v3_dot(v3_sub(rel, c->path[0]), c->entrance) > 0) return -1;
    for (int s = 0; s + 1 < c->npath; s++) {
        v3 a = c->path[s], u = v3_sub(c->path[s + 1], a);
        float l2 = v3_dot(u, u);
        float t = l2 > 0 ? v3_dot(v3_sub(rel, a), u) / l2 : 0;
        if (s == 0 && t < 0) continue;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        v3 q = v3_sub(rel, v3_add(a, v3_scale(u, t)));
        float rad = c->radius[s] + (c->radius[s + 1] - c->radius[s]) * t;
        float d = v3_len(q) - rad;   /* compare how far inside the local radius each segment puts us */
        if (d < bd) { bd = d; best = s; *perp = q; *axis_dir = v3_norm(u); *radius = rad; *dist = v3_len(q); }
    }
    return best;
}

int cave_inside(const cave *c, v3 rel, float margin, v3 *n, float *pen) {
    *pen = 0; *n = v3_(0, 1, 0);
    if (!c->enabled) return 0;
    v3 perp, u; float d, rad;
    int seg = closest_segment(c, rel, &perp, &d, &u, &rad);
    if (seg >= 0 && d < rad) {
        float gap = rad - d;
        if (gap < margin) { *pen = margin - gap; *n = d > 1e-4f ? v3_scale(perp, -1.0f / d) : v3_(0, 1, 0); }
        return 1;
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
    v3 perp, u; float d, rad;
    int seg = closest_segment(c, rel, &perp, &d, &u, &rad);
    if (seg < 0 || d >= rad) return -1;
    /* the ray rel - s*up meets the cylinder |perp(s)| = rad: perp(s) = perp - s*w, w = up minus
       its part along the axis */
    v3 w = v3_tangent(up, u);
    float A = v3_dot(w, w), B = -2.0f * v3_dot(perp, w), C = v3_dot(perp, perp) - rad * rad;
    if (A < 1e-4f) return -1;   /* looking straight along the tunnel: no floor that way */
    float disc = B * B - 4 * A * C;
    if (disc < 0) return -1;
    float s = (-B + sqrtf(disc)) / (2 * A);   /* the root ahead of us (C < 0 inside, so one root is positive) */
    if (s < 0 || s > max_s) return -1;
    return s;
}
