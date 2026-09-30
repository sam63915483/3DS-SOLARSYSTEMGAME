#include "fireflies.h"
#include "fight.h"   /* frandf */
#include <string.h>

#define SPAWN_MIN   3.0f
#define SPAWN_MAX  35.0f
#define DESPAWN    45.0f
#define NEAR_GROUND 120.0f
#define DARK       (-0.06f)   /* sun this far below the horizon (dot with the sun) counts as night */

void fireflies_init(fireflies *s) {
    memset(s, 0, sizeof *s);
    s->body = -1;
    s->rng = 0xF1EEF1EEu;
}

/* Night at this spot of the body: the sun is below its horizon. */
static int dark_at(const world *w, int bi, v3 dir) {
    v3 sun = v3_norm(v3_sub(w->b[BODY_SUN].pos, w->b[bi].pos));
    return v3_dot(dir, sun) < DARK;
}

static int on_land(const world *w, int bi, v3 dir) {
    float sea = world_sea_radius(w, bi);
    return sea < 0 || world_surface(w, bi, dir) > sea + 0.3f;
}

static void spawn(fireflies *s, const world *w, const player *p, int i) {
    firefly *f = &s->f[i];
    v3 up = p->up, t1 = v3_any_perp(up), t2 = v3_cross(up, t1);
    float a = frandf(&s->rng, 0, 6.2831853f), r = frandf(&s->rng, SPAWN_MIN, SPAWN_MAX);
    v3 pos = v3_add(v3_sub(p->pos, w->b[p->dom].pos), v3_add(v3_scale(t1, cosf(a) * r), v3_scale(t2, sinf(a) * r)));
    v3 dir = v3_norm(pos);
    if (!dark_at(w, p->dom, dir) || !on_land(w, p->dom, dir)) return;
    f->alive = 1; f->dying = 0; f->life = 0;
    f->dir = dir;
    f->vel = v3_(0, 0, 0);
    f->h0 = frandf(&s->rng, 0.6f, 3.0f); f->h = f->h0;
    f->phase = frandf(&s->rng, 0, 6.2831853f);
    f->rate = frandf(&s->rng, 1.2f, 3.5f);
}

void fireflies_update(fireflies *s, const world *w, const player *p, float dt) {
    const body *b = &w->b[p->dom];
    s->t += dt;
    int near = b->atmo && p->altitude < NEAR_GROUND;   /* planets only, and only near the ground */
    if (!near || s->body != p->dom) {
        for (int i = 0; i < FIREFLY_N; i++) s->f[i].alive = 0;
        s->body = near ? p->dom : -1;
        if (!near) return;
    }
    for (int i = 0; i < FIREFLY_N; i++) {
        firefly *f = &s->f[i];
        if (!f->alive) { spawn(s, w, p, i); continue; }
        /* drift: a lazy random walk, capped, kept tangent */
        v3 kick = v3_(frandf(&s->rng, -1, 1), frandf(&s->rng, -1, 1), frandf(&s->rng, -1, 1));
        f->vel = v3_add(v3_scale(f->vel, 1.0f - 0.8f * dt), v3_scale(v3_tangent(kick, f->dir), 1.5f * dt));
        float sp = v3_len(f->vel);
        if (sp > 1.2f) f->vel = v3_scale(f->vel, 1.2f / sp);
        float surf = world_surface(w, p->dom, f->dir);
        v3 pos = v3_add(v3_scale(f->dir, surf + f->h), v3_scale(f->vel, dt));
        f->dir = v3_norm(pos);
        f->h = f->h0 + 0.5f * sinf(s->t * 1.3f + f->phase);
        /* fade in; fade out when the spot is lit, wet or far away, then respawn */
        v3 world_pos = v3_add(b->pos, v3_scale(f->dir, world_surface(w, p->dom, f->dir) + f->h));
        if (!f->dying && (!dark_at(w, p->dom, f->dir) || !on_land(w, p->dom, f->dir) || v3_len(v3_sub(world_pos, p->pos)) > DESPAWN)) f->dying = 1;
        if (f->dying) { f->life -= 1.5f * dt; if (f->life <= 0) f->alive = 0; }
        else if (f->life < 1) { f->life += dt; if (f->life > 1) f->life = 1; }
    }
}

float fireflies_get(const fireflies *s, const world *w, int i, v3 *pos) {
    const firefly *f = &s->f[i];
    if (!f->alive || s->body < 0) return 0;
    *pos = v3_add(w->b[s->body].pos, v3_scale(f->dir, world_surface(w, s->body, f->dir) + f->h));
    float k = 0.5f + 0.5f * sinf(s->t * f->rate + f->phase);
    return f->life * k * k * k;   /* short bright blinks */
}
