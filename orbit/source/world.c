#include "world.h"
#include "noise.h"
#include <stdlib.h>

#define MU_SUN 200000.0f

static void body_place(world *w, int i, float t) {
    body *b = &w->b[i];
    if (b->orbit_r <= 0 || b->parent < 0) { b->pos = v3_(0, 0, 0); b->vel = v3_(0, 0, 0); return; }
    float a = b->orbit_phase + b->omega * t;
    float c = cosf(a), s = sinf(a);
    float ct = cosf(b->tilt), st = sinf(b->tilt);
    /* circle in the xz plane, then tilted about x; parents are placed first (lower index) */
    v3 local = v3_(b->orbit_r * c, -b->orbit_r * s * st, b->orbit_r * s * ct);
    v3 lvel = v3_(-b->orbit_r * b->omega * s, -b->orbit_r * b->omega * c * st, b->orbit_r * b->omega * c * ct);
    const body *par = &w->b[b->parent];
    b->pos = v3_add(par->pos, local);
    b->vel = v3_add(par->vel, lvel);
}

v3 world_grid_dir(const world *w, int i, int ring, int seg) {
    const body *b = &w->b[i];
    float th = (float)M_PI * ring / b->rings, ph = 2.0f * (float)M_PI * seg / b->segs;
    return v3_(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
}

void world_init(world *w) {
    /* moons are half their planet's radius, as close in as they can be while their inner edge
       still clears the atmosphere shell by 35+ units and their own pull wins for 20-40 units
       above the surface; planets are spaced so no moon's path can ever reach a neighbour */
    static const body defs[BODY_COUNT] = {
        { .name = "Sun",     .radius = 100.0f, .mu = MU_SUN,    .parent = -1,           .emissive = 1, .terrain = TERRAIN_SUN,
          .rings = 32, .segs = 48, .seed = 11u },
        { .name = "Ember",   .radius = 80.0f,  .mu = 96000.0f,  .parent = BODY_SUN,     .orbit_r = 400.0f,  .orbit_phase = 0.6f,
          .terrain = TERRAIN_EMBER,   .amp = 6.0f,  .freq = 2.4f, .sea_level = -9.0f, .liquid = 0, .rings = 40, .segs = 60, .seed = 23u,
          .atmo = 0xE09058FFu, .atmo_h = 24.0f },
        { .name = "Verdant", .radius = 128.0f, .mu = 212992.0f, .parent = BODY_SUN,     .orbit_r = 1100.0f, .orbit_phase = 2.4f,
          .terrain = TERRAIN_VERDANT, .amp = 10.0f, .freq = 2.2f, .sea_level = -1.0f, .liquid = 1, .rings = 64, .segs = 96, .seed = 37u,
          .atmo = 0x6AA0F0FFu, .atmo_h = 45.0f },
        { .name = "Frost",   .radius = 96.0f,  .mu = 129024.0f, .parent = BODY_SUN,     .orbit_r = 1900.0f, .orbit_phase = 4.4f,
          .terrain = TERRAIN_FROST,   .amp = 5.0f,  .freq = 2.4f, .sea_level = -1.0f, .liquid = 0, .rings = 48, .segs = 72, .seed = 51u,
          .atmo = 0xB8D0F0FFu, .atmo_h = 30.0f },
        { .name = "Cinder",  .radius = 40.0f,  .mu = 9600.0f,   .parent = BODY_EMBER,   .orbit_r = 190.0f,  .orbit_phase = 1.1f, .tilt = 0.35f,
          .terrain = TERRAIN_MOON,    .amp = 3.2f,  .freq = 3.0f, .sea_level = -100.0f, .liquid = 0, .rings = 40, .segs = 60, .seed = 61u },
        { .name = "Fern",    .radius = 64.0f,  .mu = 24576.0f,  .parent = BODY_VERDANT, .orbit_r = 280.0f,  .orbit_phase = 3.9f, .tilt = 0.5f,
          .terrain = TERRAIN_MOON,    .amp = 5.0f,  .freq = 3.0f, .sea_level = -100.0f, .liquid = 0, .rings = 48, .segs = 72, .seed = 67u },
        { .name = "Sleet",   .radius = 48.0f,  .mu = 13824.0f,  .parent = BODY_FROST,   .orbit_r = 220.0f,  .orbit_phase = 5.3f, .tilt = 0.25f,
          .terrain = TERRAIN_MOON,    .amp = 4.0f,  .freq = 3.0f, .sea_level = -100.0f, .liquid = 0, .rings = 44, .segs = 66, .seed = 71u },
    };
    w->t = 0;
    for (int i = 0; i < BODY_COUNT; i++) {
        body *b = &w->b[i];
        *b = defs[i];
        /* circular orbit: centripetal acceleration equals the parent's gravity */
        float mu_par = b->parent >= 0 ? w->b[b->parent].mu : 0;
        b->omega = b->orbit_r > 0 && b->parent >= 0 ? sqrtf(mu_par / (b->orbit_r * b->orbit_r * b->orbit_r)) : 0;
        body_place(w, i, 0);
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
        body_place(w, i, w->t);
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

/* Walk down from the sun: a child owns the space inside its sphere of influence, the
   distance where its own pull beats the parent's tidal pull, r = orbit * cbrt(mu / 2 mu_parent).
   Comparing raw pulls instead would leave a moon almost no room on its planet-facing side. */
int world_dominant(const world *w, v3 p) {
    int cur = BODY_SUN;
    for (;;) {
        int next = -1; float best = 1e30f;
        for (int i = 0; i < BODY_COUNT; i++) {
            const body *b = &w->b[i];
            if (b->parent != cur) continue;
            float d = v3_len(v3_sub(p, b->pos));
            float soi = b->orbit_r * cbrtf(b->mu / (2.0f * w->b[cur].mu));
            if (d < soi && d < best) { best = d; next = i; }
        }
        if (next < 0) return cur;
        cur = next;
    }
}

v3 world_frame_accel(const world *w, int i) {
    v3 a = v3_(0, 0, 0);
    for (int j = i; w->b[j].parent >= 0; j = w->b[j].parent)
        a = v3_add(a, world_gravity_from(w, w->b[j].parent, w->b[j].pos));
    return a;
}

/* ---- terrain ---- */

/* Bowls with raised rims scattered over a moon. Bigger craters are deeper. Heights stay within
   about -1.8 amp .. +0.6 amp, so amp remains a valid upper bound for collision. */
static float moon_craters(v3 dir, uint32_t seed, float amp) {
    float h = 0;
    for (int k = 0; k < 22; k++) {
        v3 c = v3_(noise_hashf(k, 0, 77, seed) * 2 - 1, noise_hashf(k, 1, 77, seed) * 2 - 1, noise_hashf(k, 2, 77, seed) * 2 - 1);
        float l = v3_len(c);
        if (l < 0.15f) continue;
        c = v3_scale(c, 1.0f / l);
        float u = noise_hashf(k, 3, 77, seed);
        float R = 0.10f + u * u * 0.35f;                    /* angular radius, mostly small */
        float d = v3_dot(dir, c);
        if (d > 1) d = 1;
        if (d < -1) d = -1;
        float x = acosf(d) / R;                             /* 0 at the centre, 1 at the rim */
        if (x > 1.4f) continue;
        float depth = amp * (0.5f + 1.2f * R / 0.45f);
        if (x < 1) h += depth * (x * x - 1);                /* bowl */
        float r = (x - 1.0f) / 0.2f;
        h += depth * 0.3f * expf(-r * r);                   /* rim */
    }
    return h;
}

float world_height_raw(const world *w, int i, v3 dir) {
    const body *b = &w->b[i];
    if (b->amp <= 0) return 0;
    v3 p = v3_add(v3_scale(dir, b->freq), v3_(7.3f, 1.9f, 4.1f));
    float n = noise_fbm(p, 3, b->seed);
    if (b->terrain == TERRAIN_MOON) return (n - 0.5f) * 0.6f * b->amp + moon_craters(dir, b->seed, b->amp);
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
    /* the detail texture darkens by about 10 percent on average; pre-brighten to compensate */
    r *= 1.1f; g *= 1.1f; b *= 1.1f;
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
    case TERRAIN_MOON: {
        float k = b->amp > 0 ? raw_h / b->amp : 0;   /* about -1.8 in crater floors .. 0.6 on rims */
        float g = 148 + v * 36 + k * 28;             /* dark bowls, pale rims */
        return rgb(g, g, g + 8);
    }
    case TERRAIN_FROST:
        if (t <= 0.001f) return mix3(150, 185, 220, 175, 205, 232, 0.5f + v);       /* frozen lake */
        if (t < 0.6f)   return rgb(222 + v * 16, 232 + v * 12, 244);                /* snowfield */
        return mix3(222, 232, 244, 250, 252, 255, (t - 0.6f) / 0.6f);
    }
    return 0xFFFFFFFF;
}
