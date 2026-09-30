#include "minitest.h"
#include "world.h"
#include "player.h"
#include "fireflies.h"
#include "mesh.h"
#include "dust.h"
#include <string.h>

#define DT (1.0f / 60.0f)

/* Finds a spot on Verdant's land on the given side of the sun (sign +1 day, -1 night). */
static v3 land_on(const world *w, v3 sun, float sign) {
    const body *b = &w->b[BODY_VERDANT];
    for (int k = 0; k < 4000; k++) {
        float a = k * 0.37f, e = k * 0.11f;
        v3 d = v3_norm(v3_(cosf(a) * cosf(e), sinf(e), sinf(a) * cosf(e)));
        if (v3_dot(d, sun) * sign > 0.4f && world_height_raw(w, BODY_VERDANT, d) > b->sea_level + 2.0f) return d;
    }
    return v3_scale(sun, sign);
}

int main(void) {
    world w; world_init(&w);
    const body *b = &w.b[BODY_VERDANT];
    v3 sun = v3_norm(v3_sub(w.b[BODY_SUN].pos, b->pos));

    /* fireflies: only on the night side of a planet, only over land */
    player p; memset(&p, 0, sizeof p);
    v3 up = land_on(&w, sun, -1.0f);
    p.dom = BODY_VERDANT; p.up = p.body_up = up;
    p.pos = v3_add(b->pos, v3_scale(up, world_surface(&w, BODY_VERDANT, up) + PLAYER_HALF_HEIGHT));
    p.grounded = 1; p.altitude = 0;
    fireflies ff; fireflies_init(&ff);
    for (int i = 0; i < 600; i++) fireflies_update(&ff, &w, &p, DT);
    int alive = 0, lit = 0, wet = 0, bright = 0;
    for (int i = 0; i < FIREFLY_N; i++) {
        v3 pos;
        float k = fireflies_get(&ff, &w, i, &pos);
        if (!ff.f[i].alive) continue;
        alive++;
        if (k > 0.5f) bright++;
        if (v3_dot(ff.f[i].dir, sun) > -0.06f) lit++;
        if (world_surface(&w, BODY_VERDANT, ff.f[i].dir) < world_sea_radius(&w, BODY_VERDANT)) wet++;
    }
    printf("     (%d fireflies out, %d bright now)\n", alive, bright);
    CHECK(alive > 15, "fireflies come out on the night side");
    CHECK(lit == 0 && wet == 0, "none over the day side or the sea");

    /* on the day side none appear */
    v3 day = land_on(&w, sun, 1.0f);
    p.up = p.body_up = day;
    p.pos = v3_add(b->pos, v3_scale(day, world_surface(&w, BODY_VERDANT, day) + PLAYER_HALF_HEIGHT));
    for (int i = 0; i < 300; i++) fireflies_update(&ff, &w, &p, DT);
    alive = 0;
    for (int i = 0; i < FIREFLY_N; i++) if (ff.f[i].alive) alive++;
    CHECK(alive == 0, "in daylight every firefly has faded out");

    /* the frame fix that came with them: nothing is felt at a body's centre but tides */
    v3 fm = world_frame_accel(&w, BODY_CINDER);
    v3 felt = v3_sub(v3_sub(world_gravity_at(&w, w.b[BODY_CINDER].pos), world_gravity_from(&w, BODY_CINDER, w.b[BODY_CINDER].pos)), fm);
    CHECK(v3_len(felt) < 1e-4f, "at Cinder's centre the frame cancels every other pull");
    /* foam hugs the shoreline; dust puffs age out */
    {
        mesh fm = mesh_foam(&w, BODY_VERDANT);
        int bad = 0;
        for (int i = 0; i < fm.count; i++) {
            v3 q = v3_(fm.v[i].pos[0], fm.v[i].pos[1], fm.v[i].pos[2]);
            float r = v3_len(q), sea = world_sea_radius(&w, BODY_VERDANT);
            /* never under the sea; over land, never floating more than a unit above it (the seabed under the sea-side edge is of course far below) */
            if (r < sea - 0.01f || (r > sea + 0.2f && r > world_surface(&w, BODY_VERDANT, v3_norm(q)) + 1.0f)) bad++;
        }
        printf("     (%d foam vertices)\n", fm.count);
        CHECK(fm.count > 300 && bad == 0, "foam follows the shoreline, never under the sea or high above the land");
        mesh_free(&fm);
        mesh nb = mesh_nebula(4000.0f, 1u);
        CHECK(nb.count > 500, "the nebula sphere exists");
        mesh_free(&nb);
        dust d; dust_init(&d);
        dust_burst(&d, &w, BODY_VERDANT, w.b[BODY_VERDANT].pos, v3_(0, 1, 0), 10, 3.0f, 0.5f);
        int alive = 0; float sz;
        for (int i = 0; i < DUST_N; i++) if (dust_get(&d, i, &sz) > 0) alive++;
        CHECK(alive == 10, "a burst makes ten puffs");
        for (int i = 0; i < 90; i++) dust_update(&d, &w, DT);
        alive = 0;
        for (int i = 0; i < DUST_N; i++) if (dust_get(&d, i, &sz) > 0) alive++;
        CHECK(alive == 0, "and they are gone within 1.5 s");
    }
    MT_END();
}
