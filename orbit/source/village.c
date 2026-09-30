#include "village.h"
#include <string.h>

#define PLAYER_R 0.35f

void village_setup(world *w, int bi) {
    village *v = &w->village;
    memset(v, 0, sizeof *v);
    v->body = bi;
    const body *b = &w->b[bi];
    v3 spawn = world_spawn_dir(w, bi);
    v3 axis = v3_any_perp(spawn), axis2 = v3_cross(spawn, axis);
    /* two spots a short walk from the spawn, on land; pads are off while we look (n is 0) */
    float want = b->sea_level + 0.15f * b->amp;
    const float offs[HOUSE_N][2] = { { 24.0f, 6.5f }, { -7.5f, 25.5f } };   /* units along the ground about the two axes */
    for (int k = 0; k < HOUSE_N; k++) {
        v3 dir = v3_norm(v3_rotate(v3_rotate(spawn, axis, offs[k][0] / b->radius), axis2, offs[k][1] / b->radius));
        for (int tries = 0; tries < 40 && world_height_raw(w, bi, dir) < want; tries++)
            dir = v3_norm(v3_rotate(dir, axis2, 0.04f));
        house *h = &v->h[k];
        h->dir = dir;
        h->pad_h = world_height_raw(w, bi, dir);
        h->style = k;
        /* the door faces the spawn */
        v3 f = v3_tangent(v3_sub(spawn, dir), dir);
        h->fwd = v3_len(f) > 1e-3f ? v3_norm(f) : v3_any_perp(dir);
        h->npc_local = k == 0 ? v3_(-1.4f, 0, -1.2f) : v3_(1.2f, 0, -1.0f);
        h->npc_fwd = h->fwd;
    }
    v->n = HOUSE_N;
}

void village_frame(const world *w, int k, v3 *origin, v3 *right, v3 *up, v3 *fwd) {
    const village *v = &w->village;
    const body *b = &w->b[v->body];
    const house *h = &v->h[k];
    *up = h->dir;
    *fwd = v3_norm(v3_tangent(h->fwd, h->dir));
    *right = v3_cross(*up, *fwd);
    *origin = v3_add(b->pos, v3_scale(h->dir, b->radius + h->pad_h));
}

static v3 to_local(v3 p, v3 origin, v3 right, v3 up, v3 fwd) {
    v3 d = v3_sub(p, origin);
    return v3_(v3_dot(d, right), v3_dot(d, up), v3_dot(d, fwd));
}
static v3 from_local(v3 l, v3 right, v3 up, v3 fwd) {
    return v3_add(v3_add(v3_scale(right, l.x), v3_scale(up, l.y)), v3_scale(fwd, l.z));
}

/* Pushes the local point out of a wall slab |axis - at| < thick (within the slab's extent
   along the other axis and below top), whichever side it is nearer. */
static void slab_push(v3 *l, v3 *vel_l, int axis, float at, float thick, float other_min, float other_max, float top) {
    float a = axis == 0 ? l->x : l->z, o = axis == 0 ? l->z : l->x;
    if (l->y > top || o < other_min || o > other_max) return;
    float d = a - at;
    if (fabsf(d) >= thick + PLAYER_R) return;
    float push = (d >= 0 ? thick + PLAYER_R : -(thick + PLAYER_R)) - d;
    if (axis == 0) { l->x += push; if ((vel_l->x > 0) == (push < 0)) vel_l->x = 0; }
    else { l->z += push; if ((vel_l->z > 0) == (push < 0)) vel_l->z = 0; }
}

void village_collide(const world *w, player *p) {
    const village *v = &w->village;
    if (!v->n || p->dom != v->body) return;
    const body *b = &w->b[v->body];
    for (int k = 0; k < v->n; k++) {
        const house *h = &v->h[k];
        if (v3_dot(v3_norm(v3_sub(p->pos, b->pos)), h->dir) < 0.99f) continue;   /* nowhere near */
        v3 origin, right, up, fwd;
        village_frame(w, k, &origin, &right, &up, &fwd);
        v3 l = to_local(p->pos, origin, right, up, fwd);
        v3 vl = v3_(v3_dot(p->vel, right), v3_dot(p->vel, up), v3_dot(p->vel, fwd));
        /* the alien: a post you cannot walk through */
        {
            v3 d = v3_sub(l, h->npc_local); d.y = 0;
            float dist = v3_len(d), min = 0.5f + PLAYER_R;
            if (dist < min && l.y < 2.6f) {
                v3 n = dist > 1e-4f ? v3_scale(d, 1.0f / dist) : v3_(1, 0, 0);
                l = v3_add(l, v3_scale(n, min - dist));
                float into = v3_dot(vl, n);
                if (into < 0) vl = v3_sub(vl, v3_scale(n, into));
            }
        }
        if (h->style == 0) {
            /* the dome: a round wall, open where the door is */
            float r = sqrtf(l.x * l.x + l.z * l.z);
            float ang = atan2f(l.x, l.z);   /* 0 straight out the door */
            int in_door = fabsf(ang) < DOME_DOOR && l.y < 2.6f;
            if (!in_door && l.y < DOME_R && fabsf(r - DOME_R) < 0.3f + PLAYER_R && r > 1e-4f) {
                float d = r - DOME_R;
                float push = (d >= 0 ? 0.3f + PLAYER_R : -(0.3f + PLAYER_R)) - d;
                v3 n = v3_(l.x / r, 0, l.z / r);
                l = v3_add(l, v3_scale(n, push));
                float into = v3_dot(vl, n);
                if ((into > 0) == (push < 0)) vl = v3_sub(vl, v3_scale(n, into));
            }
        } else {
            /* the lodge: four wall slabs, the front one with a gap for the door */
            float t = 0.15f;
            slab_push(&l, &vl, 0, -LODGE_HX, t, -LODGE_HZ - 0.5f, LODGE_HZ + 0.5f, LODGE_ROOF);
            slab_push(&l, &vl, 0, LODGE_HX, t, -LODGE_HZ - 0.5f, LODGE_HZ + 0.5f, LODGE_ROOF);
            slab_push(&l, &vl, 2, -LODGE_HZ, t, -LODGE_HX - 0.5f, LODGE_HX + 0.5f, LODGE_ROOF);
            if (!(fabsf(l.x) < LODGE_DOOR - 0.05f && l.y < LODGE_DOOR_H))
                slab_push(&l, &vl, 2, LODGE_HZ, t, -LODGE_HX - 0.5f, LODGE_HX + 0.5f, LODGE_ROOF);
        }
        p->pos = v3_add(origin, from_local(l, right, up, fwd));
        p->vel = from_local(vl, right, up, fwd);
    }
}

void village_update(world *w, const player *p, float dt) {
    village *v = &w->village;
    if (!v->n) return;
    for (int k = 0; k < v->n; k++) {
        house *h = &v->h[k];
        v3 origin, right, up, fwd;
        village_frame(w, k, &origin, &right, &up, &fwd);
        v3 npc = v3_add(origin, from_local(h->npc_local, right, up, fwd));
        v3 to = v3_tangent(v3_sub(p->pos, npc), up);
        float d = v3_len(to);
        v3 want = d < 7.0f && d > 1e-3f ? v3_scale(to, 1.0f / d) : fwd;   /* look at a visitor, else at the door */
        h->npc_fwd = v3_norm(v3_tangent(v3_lerp(h->npc_fwd, want, ease(3.0f, dt)), up));
    }
}

const char *village_talk(const world *w, const player *p) {
    const village *v = &w->village;
    static const char *lines[HOUSE_N] = { "VESK: THE FISH BITE BEST AT DUSK, TRAVELLER.", "MOMO: THE MOONS ARE HOLLOW. I HAVE HEARD THEM HUM." };
    if (!v->n || p->dom != v->body) return NULL;
    for (int k = 0; k < v->n; k++) {
        v3 origin, right, up, fwd;
        village_frame(w, k, &origin, &right, &up, &fwd);
        v3 npc = v3_add(origin, from_local(v->h[k].npc_local, right, up, fwd));
        if (v3_len(v3_sub(p->pos, npc)) < 3.2f) return lines[k];
    }
    return NULL;
}
