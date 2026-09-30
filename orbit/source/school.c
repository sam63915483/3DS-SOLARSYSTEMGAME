#include "school.h"
#include "fight.h"   /* frandf */
#include <string.h>

#define SPAWN_MIN   6.0f    /* units from the player */
#define SPAWN_MAX  40.0f
#define DESPAWN    60.0f
#define WATER_MIN   1.2f    /* clear water a fish needs under it */
#define NEAR_SEA  150.0f    /* the player must be within this of the surface to bother */

void school_init(school *s) {
    memset(s, 0, sizeof *s);
    s->body = -1;
    s->rng = 0xC0FFEE11u;
}

static float sea_depth_at(const world *w, int bi, v3 dir) {
    return world_sea_radius(w, bi) - world_surface(w, bi, dir);
}

/* Try once to put fish i in open water near the player. */
static void spawn(school *s, const world *w, const player *p, int i) {
    const body *b = &w->b[p->dom];
    school_fish *f = &s->f[i];
    v3 up = p->up, t1 = v3_any_perp(up), t2 = v3_cross(up, t1);
    float a = frandf(&s->rng, 0, 6.2831853f), r = frandf(&s->rng, SPAWN_MIN, SPAWN_MAX);
    v3 pos = v3_add(v3_sub(p->pos, b->pos), v3_add(v3_scale(t1, cosf(a) * r), v3_scale(t2, sinf(a) * r)));
    v3 dir = v3_norm(pos);
    float depth = frandf(&s->rng, 0.3f, 1.4f);
    if (sea_depth_at(w, p->dom, dir) < depth + WATER_MIN) return;   /* too shallow: try again next frame */
    f->alive = 1;
    f->depth = depth;
    f->rel = v3_scale(dir, world_sea_radius(w, p->dom) - depth);
    v3 h = v3_rotate(v3_any_perp(dir), dir, frandf(&s->rng, 0, 6.2831853f));
    f->fwd = v3_norm(h);
    f->speed = frandf(&s->rng, 0.8f, 2.0f);
    f->scale = frandf(&s->rng, 1.3f, 2.8f);
    f->phase = frandf(&s->rng, 0, 6.2831853f);
    f->turn = 0;
}

void school_update(school *s, const world *w, const player *p, float dt) {
    const body *b = &w->b[p->dom];
    float sea_r = world_sea_radius(w, p->dom);
    int near = sea_r > 0 && (v3_len(v3_sub(p->pos, b->pos)) - sea_r) < NEAR_SEA;
    if (!near || s->body != p->dom) {
        for (int i = 0; i < SCHOOL_N; i++) s->f[i].alive = 0;
        s->body = near ? p->dom : -1;
        if (!near) return;
    }
    for (int i = 0; i < SCHOOL_N; i++) {
        school_fish *f = &s->f[i];
        if (!f->alive) { spawn(s, w, p, i); continue; }
        /* wander: the heading random-walks, the body wiggles */
        f->turn += (frandf(&s->rng, -1, 1) * 2.0f - f->turn * 0.8f) * dt;
        f->phase += (4.0f + f->speed * 2.0f) * dt;
        v3 dir = v3_norm(f->rel);
        f->fwd = v3_norm(v3_tangent(v3_rotate(f->fwd, dir, f->turn * dt), dir));
        f->rel = v3_add(f->rel, v3_scale(f->fwd, f->speed * dt));
        /* stay on the sphere at its depth */
        dir = v3_norm(f->rel);
        f->rel = v3_scale(dir, sea_r - f->depth);
        /* keep clear of the bed: turn around when the water runs out ahead */
        v3 ahead = v3_norm(v3_add(f->rel, v3_scale(f->fwd, 2.0f)));
        if (sea_depth_at(w, p->dom, ahead) < f->depth + WATER_MIN) {
            f->fwd = v3_scale(f->fwd, -1.0f);
            f->turn = frandf(&s->rng, -1.5f, 1.5f);
            if (sea_depth_at(w, p->dom, dir) < f->depth + 0.4f) f->alive = 0;   /* already in the bed: vanish */
        }
        if (v3_len(v3_sub(v3_add(b->pos, f->rel), p->pos)) > DESPAWN) f->alive = 0;
    }
}

void school_fish_basis(const school *s, const world *w, int i, v3 *pos, v3 *up, v3 *fwd) {
    const school_fish *f = &s->f[i];
    v3 dir = v3_norm(f->rel);
    *pos = v3_add(w->b[s->body].pos, f->rel);
    *up = dir;
    *fwd = v3_norm(v3_tangent(v3_rotate(f->fwd, dir, sinf(f->phase) * 0.18f), dir));
}
