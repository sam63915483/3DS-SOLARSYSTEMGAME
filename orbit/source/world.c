#include "world.h"

#define MU_SUN 200000.0f

static void body_place(body *b, float t) {
    if (b->orbit_r <= 0) { b->pos = v3_(0, 0, 0); b->vel = v3_(0, 0, 0); return; }
    float a = b->orbit_phase + b->omega * t;
    float c = cosf(a), s = sinf(a);
    b->pos = v3_(b->orbit_r * c, 0, b->orbit_r * s);
    b->vel = v3_(-b->orbit_r * b->omega * s, 0, b->orbit_r * b->omega * c);
}

void world_init(world *w) {
    static const body defs[BODY_COUNT] = {
        { "Sun",    100.0f, MU_SUN,  0,      0.0f, 0, 0xFFE070FF, 0xFFC848FF, 1, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Ember",   80.0f, 96000.0f, 320.0f, 0.6f, 0, 0xC84828FF, 0x8A2E18FF, 0, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Verdant", 128.0f, 212992.0f, 620.0f, 2.4f, 0, 0x3A9A4AFF, 0x2A6C9AFF, 0, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Frost",   96.0f, 129024.0f, 940.0f, 4.4f, 0, 0xD8E8F4FF, 0x8AAEC8FF, 0, {0,0,0}, {0,0,0}, {0,0,0} },
    };
    w->t = 0;
    for (int i = 0; i < BODY_COUNT; i++) {
        body *b = &w->b[i];
        *b = defs[i];
        /* circular orbit: centripetal acceleration equals the sun's gravity */
        b->omega = b->orbit_r > 0 ? sqrtf(MU_SUN / (b->orbit_r * b->orbit_r * b->orbit_r)) : 0;
        body_place(b, 0);
        b->prev = b->pos;
    }
}

void world_update(world *w, float dt) {
    w->t += dt;
    for (int i = 0; i < BODY_COUNT; i++) {
        w->b[i].prev = w->b[i].pos;
        body_place(&w->b[i], w->t);
    }
}

v3 world_gravity_from(const world *w, int i, v3 p) {
    v3 d = v3_sub(w->b[i].pos, p);
    float r2 = v3_dot(d, d);
    float rmin = w->b[i].radius * 0.5f;
    if (r2 < rmin * rmin) r2 = rmin * rmin;   /* never blow up inside a body */
    float r = sqrtf(r2);
    return v3_scale(d, w->b[i].mu / (r2 * r));
}

v3 world_gravity_at(const world *w, v3 p) {
    v3 g = v3_(0, 0, 0);
    for (int i = 0; i < BODY_COUNT; i++) g = v3_add(g, world_gravity_from(w, i, p));
    return g;
}

int world_dominant(const world *w, v3 p) {
    int best = 0; float best_g = -1;
    for (int i = 0; i < BODY_COUNT; i++) {
        v3 d = v3_sub(w->b[i].pos, p);
        float r2 = v3_dot(d, d);
        if (r2 < 1e-4f) r2 = 1e-4f;
        float g = w->b[i].mu / r2;
        if (g > best_g) { best_g = g; best = i; }
    }
    return best;
}
