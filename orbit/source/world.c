#include "world.h"
#include "noise.h"

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
        /* name     radius  mu         orbit   phase omega emis terrain          amp   freq  sea seed */
        { "Sun",    100.0f, MU_SUN,    0,      0.0f, 0, 1, TERRAIN_SUN,     0.0f, 6.0f, 0, 11u, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Ember",   80.0f, 96000.0f,  320.0f, 0.6f, 0, 0, TERRAIN_EMBER,   3.0f, 3.2f, 0, 23u, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Verdant", 128.0f, 212992.0f, 620.0f, 2.4f, 0, 0, TERRAIN_VERDANT, 4.0f, 2.6f, 1, 37u, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Frost",   96.0f, 129024.0f, 940.0f, 4.4f, 0, 0, TERRAIN_FROST,   2.5f, 3.0f, 1, 51u, {0,0,0}, {0,0,0}, {0,0,0} },
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

/* ---- terrain ---- */

float world_height_raw(const world *w, int i, v3 dir) {
    const body *b = &w->b[i];
    if (b->amp <= 0) return 0;
    v3 p = v3_add(v3_scale(dir, b->freq), v3_(7.3f, 1.9f, 4.1f));
    float n = noise_fbm(p, 3, b->seed);
    return (n - 0.5f) * 2.0f * b->amp;
}

float world_height(const world *w, int i, v3 dir) {
    float h = world_height_raw(w, i, dir);
    if (w->b[i].has_sea && h < 0) h = 0;
    return h;
}

float world_surface(const world *w, int i, v3 dir) {
    return w->b[i].radius + world_height(w, i, dir);
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static uint32_t rgb(float r, float g, float b) {
    return ((uint32_t)clampf(r, 0, 255) << 24) | ((uint32_t)clampf(g, 0, 255) << 16) |
           ((uint32_t)clampf(b, 0, 255) << 8) | 0xFF;
}
static uint32_t mix3(float r0, float g0, float b0, float r1, float g1, float b1, float t) {
    t = clampf(t, 0, 1);
    return rgb(r0 + (r1 - r0) * t, g0 + (g1 - g0) * t, b0 + (b1 - b0) * t);
}

uint32_t world_color(const world *w, int i, v3 dir, float raw_h) {
    const body *b = &w->b[i];
    float t = b->amp > 0 ? raw_h / b->amp : 0;   /* -1..1 */
    /* fine variation so flat areas do not look like plastic */
    float v = noise_value(v3_add(v3_scale(dir, 40.0f), v3_(3, 5, 9)), b->seed + 99u) - 0.5f;   /* -0.5..0.5 */
    switch (b->terrain) {
    case TERRAIN_SUN: {
        float g = noise_fbm(v3_scale(dir, 9.0f), 2, b->seed);
        return mix3(255, 200, 70, 255, 240, 150, g);
    }
    case TERRAIN_VERDANT:
        if (t < 0)      return mix3(24, 70, 150, 40, 110, 190, 1.0f + t);          /* deep to shallow water */
        if (t < 0.06f)  return rgb(205 + v * 20, 195 + v * 20, 140);               /* beach */
        if (t < 0.55f)  return mix3(80 + v * 30, 160 + v * 30, 60, 50, 120, 45, t / 0.55f); /* grass */
        if (t < 0.85f)  return rgb(115 + v * 20, 105 + v * 20, 95 + v * 20);       /* rock */
        return rgb(235, 240, 245);                                                 /* snow */
    case TERRAIN_EMBER:
        if (t < -0.3f)  return mix3(40, 12, 8, 110, 35, 20, (t + 1.0f) / 0.7f);    /* dark lowlands */
        if (t < 0.6f)   return mix3(110 + v * 30, 35 + v * 10, 20, 205, 95, 50, (t + 0.3f) / 0.9f);
        return mix3(205, 95, 50, 245, 190, 130, (t - 0.6f) / 0.4f);                /* bright peaks */
    case TERRAIN_FROST:
        if (t < 0)      return mix3(150, 185, 220, 175, 205, 232, 1.0f + t);        /* frozen sea */
        if (t < 0.5f)   return rgb(222 + v * 16, 232 + v * 12, 244);                /* snowfield */
        return mix3(222, 232, 244, 250, 252, 255, (t - 0.5f) / 0.5f);
    }
    return 0xFFFFFFFF;
}
