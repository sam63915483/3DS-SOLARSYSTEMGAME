#include "world.h"
#include "noise.h"
#include <stdlib.h>

#define MU_SUN 200000.0f

static void body_place(body *b, float t) {
    if (b->orbit_r <= 0) { b->pos = v3_(0, 0, 0); b->vel = v3_(0, 0, 0); return; }
    float a = b->orbit_phase + b->omega * t;
    float c = cosf(a), s = sinf(a);
    b->pos = v3_(b->orbit_r * c, 0, b->orbit_r * s);
    b->vel = v3_(-b->orbit_r * b->omega * s, 0, b->orbit_r * b->omega * c);
}

v3 world_grid_dir(const world *w, int i, int ring, int seg) {
    const body *b = &w->b[i];
    float th = (float)M_PI * ring / b->rings, ph = 2.0f * (float)M_PI * seg / b->segs;
    return v3_(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
}

void world_init(world *w) {
    static const body defs[BODY_COUNT] = {
        /* name     radius  mu         orbit   phase omega emis terrain          amp   freq  sea    liq rings segs seed */
        { "Sun",    100.0f, MU_SUN,    0,      0.0f, 0, 1, TERRAIN_SUN,     0.0f,  6.0f,  0.0f, 0, 32, 48, 11u, 0, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Ember",   80.0f, 96000.0f,  320.0f, 0.6f, 0, 0, TERRAIN_EMBER,   6.0f,  2.4f, -9.0f, 0, 40, 60, 23u, 0, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Verdant", 128.0f, 212992.0f, 620.0f, 2.4f, 0, 0, TERRAIN_VERDANT, 10.0f, 2.2f, -1.0f, 1, 64, 96, 37u, 0, {0,0,0}, {0,0,0}, {0,0,0} },
        { "Frost",   96.0f, 129024.0f, 940.0f, 4.4f, 0, 0, TERRAIN_FROST,   5.0f,  2.4f, -1.0f, 0, 48, 72, 51u, 0, {0,0,0}, {0,0,0}, {0,0,0} },
    };
    w->t = 0;
    for (int i = 0; i < BODY_COUNT; i++) {
        body *b = &w->b[i];
        *b = defs[i];
        /* circular orbit: centripetal acceleration equals the sun's gravity */
        b->omega = b->orbit_r > 0 ? sqrtf(MU_SUN / (b->orbit_r * b->orbit_r * b->orbit_r)) : 0;
        body_place(b, 0);
        b->prev = b->pos;
        /* bake the height grid: the mesh and the collision both read it */
        b->grid = malloc(sizeof(float) * (b->rings + 1) * b->segs);
        for (int r = 0; r <= b->rings; r++) {
            for (int s = 0; s < b->segs; s++) {
                float h = world_height_raw(w, i, world_grid_dir(w, i, r, s));
                if (!b->liquid && h < b->sea_level) h = b->sea_level;   /* ice sheet: flat and walkable */
                b->grid[r * b->segs + s] = h;
            }
        }
        /* the poles are one point: give every duplicate the same height */
        for (int s = 1; s < b->segs; s++) {
            b->grid[s] = b->grid[0];
            b->grid[b->rings * b->segs + s] = b->grid[b->rings * b->segs];
        }
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

float world_grid_height(const world *w, int i, int ring, int seg) {
    const body *b = &w->b[i];
    if (!b->grid) return 0;
    return b->grid[ring * b->segs + seg];
}

float world_height(const world *w, int i, v3 dir) {
    const body *b = &w->b[i];
    if (!b->grid || b->amp <= 0) return 0;
    float y = dir.y; if (y > 1) y = 1; if (y < -1) y = -1;
    float th = acosf(y), ph = atan2f(dir.z, dir.x);
    if (ph < 0) ph += 2.0f * (float)M_PI;
    float fi = th / (float)M_PI * b->rings, fj = ph / (2.0f * (float)M_PI) * b->segs;
    int i0 = (int)fi; if (i0 >= b->rings) i0 = b->rings - 1; if (i0 < 0) i0 = 0;
    int j0 = (int)fj; if (j0 >= b->segs) j0 = b->segs - 1; if (j0 < 0) j0 = 0;
    float u = fi - i0, v = fj - j0;
    int j1 = (j0 + 1) % b->segs;
    const float *g = b->grid;
    float ha = g[i0 * b->segs + j0], hb = g[(i0 + 1) * b->segs + j0];
    float hc = g[(i0 + 1) * b->segs + j1], hd = g[i0 * b->segs + j1];
    /* the mesh splits each cell into triangles a-c-b (v <= u) and a-d-c (v >= u) */
    if (v <= u) return ha + u * (hb - ha) + v * (hc - hb);
    return ha + v * (hd - ha) + u * (hc - hd);
}

float world_surface(const world *w, int i, v3 dir) {
    return w->b[i].radius + world_height(w, i, dir);
}

float world_sea_radius(const world *w, int i) {
    const body *b = &w->b[i];
    return b->liquid ? b->radius + b->sea_level : -1.0f;
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
    float t = b->amp > 0 ? (raw_h - b->sea_level) / b->amp : 0;   /* 0 at the shoreline, about -1..1 */
    /* fine variation so flat areas do not look like plastic */
    float v = noise_value(v3_add(v3_scale(dir, 40.0f), v3_(3, 5, 9)), b->seed + 99u) - 0.5f;   /* -0.5..0.5 */
    switch (b->terrain) {
    case TERRAIN_SUN: {
        float g = noise_fbm(v3_scale(dir, 9.0f), 2, b->seed);
        return mix3(255, 200, 70, 255, 240, 150, g);
    }
    case TERRAIN_VERDANT:
        if (t < 0)      return mix3(40, 70, 90, 190, 175, 125, 1.0f + t * 1.4f);   /* deep seabed to sandy shallows */
        if (t < 0.05f)  return rgb(205 + v * 20, 195 + v * 20, 140);               /* beach */
        if (t < 0.55f)  return mix3(80 + v * 30, 160 + v * 30, 60, 50, 120, 45, t / 0.55f); /* grass */
        if (t < 0.85f)  return rgb(115 + v * 20, 105 + v * 20, 95 + v * 20);       /* rock */
        return rgb(235, 240, 245);                                                 /* snow */
    case TERRAIN_EMBER:
        if (t < 0.9f)   return mix3(40, 12, 8, 110, 35, 20, t / 0.9f);             /* dark lowlands */
        if (t < 2.0f)   return mix3(110 + v * 30, 35 + v * 10, 20, 205, 95, 50, (t - 0.9f) / 1.1f);
        return mix3(205, 95, 50, 245, 190, 130, (t - 2.0f) / 0.6f);                /* bright peaks */
    case TERRAIN_FROST:
        if (t <= 0.001f) return mix3(150, 185, 220, 175, 205, 232, 0.5f + v);       /* frozen lake */
        if (t < 0.6f)   return rgb(222 + v * 16, 232 + v * 12, 244);                /* snowfield */
        return mix3(222, 232, 244, 250, 252, 255, (t - 0.6f) / 0.6f);
    }
    return 0xFFFFFFFF;
}
