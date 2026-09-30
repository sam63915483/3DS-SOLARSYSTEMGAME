#include "minitest.h"
#include "world.h"
#include "cave.h"
#include "player.h"
#include "camera.h"
#include "mesh.h"
#include "fireflies.h"
#include <string.h>

#define DT (1.0f / 60.0f)

int main(void) {
    world w; world_init(&w);
    int moons[3] = { BODY_CINDER, BODY_FERN, BODY_SLEET };

    CHECK(!w.b[BODY_VERDANT].cave.enabled, "planets have no cave");
    for (int m = 0; m < 3; m++) {
        const body *b = &w.b[moons[m]];
        const cave *c = &b->cave;
        CHECK(c->enabled, "every moon has a cave");
        CHECK(c->npath >= 6 && c->npath <= CAVE_PATH_MAX, "the tunnel has a sensible number of bends");
        /* geometry: a shaft, then a descent that ends inside the core */
        CHECK(v3_len(c->path[0]) > v3_len(c->path[1]), "the shaft goes down");
        int descends = 1;
        for (int k = 1; k + 1 < c->npath; k++) if (v3_len(c->path[k + 1]) > v3_len(c->path[k]) + 0.01f) descends = 0;
        CHECK(descends, "every bend is lower than the last");
        CHECK(v3_len(c->path[c->npath - 1]) < c->core_r, "the tunnel ends inside the core cavern");
        CHECK(v3_len(c->path[0]) < b->radius + b->amp, "the shaft top is below the moon's peaks");
        /* walkable: no bend steeper than about 45 degrees */
        int gentle = 1;
        for (int k = 1; k + 1 < c->npath; k++) {
            v3 d = v3_sub(c->path[k + 1], c->path[k]);
            float down = -v3_dot(d, v3_norm(c->path[k])), along = v3_len(d);
            if (along > 0.5f && down / along > 0.72f) gentle = 0;
        }
        CHECK(gentle, "the tunnel slopes are walkable");
        /* the pit: the mouth sits well below the ground around it */
        {
            v3 e = c->entrance, side = v3_any_perp(e);
            float at = world_height_raw(&w, moons[m], e);
            float rim = world_height_raw(&w, moons[m], v3_norm(v3_add(e, v3_scale(side, c->pit_ang * 1.5f))));
            CHECK(rim - at > 2.0f, "the cave mouth is at the bottom of a pit");
        }
        /* the hole in the mesh */
        {
            mesh pm = mesh_planet(&w, moons[m]);
            int holed = 0, total = pm.idx_count / 3;
            for (int t = 0; t + 2 < pm.idx_count; t += 3) {
                v3 centre = v3_(0, 0, 0);
                for (int k = 0; k < 3; k++) centre = v3_add(centre, v3_(pm.v[pm.idx[t + k]].pos[0], pm.v[pm.idx[t + k]].pos[1], pm.v[pm.idx[t + k]].pos[2]));
                if (world_in_hole(&w, moons[m], v3_norm(centre))) holed++;
            }
            CHECK(holed == 0, "no terrain triangle is left over the cave mouth");
            CHECK(total < b->rings * b->segs * 2 - 3, "several triangles were cut out for the mouth");
            mesh_free(&pm);
        }
        /* gravity: full at the surface, zero in the core */
        {
            v3 surf = v3_scale(c->entrance, b->radius + 5);
            float g_out = v3_len(world_gravity_from(&w, moons[m], v3_add(b->pos, surf)));
            float g_core = v3_len(world_gravity_from(&w, moons[m], v3_add(b->pos, v3_scale(c->entrance, c->core_r * 0.5f))));
            float g_mid = v3_len(world_gravity_from(&w, moons[m], c->path[c->npath / 2]));
            CHECK(g_out > 5.0f, "gravity is normal above the surface");
            CHECK_NEAR(g_core, 0.0f, 1e-4f, "gravity is zero inside the core");
            CHECK(g_mid > 0 && g_mid < g_out, "gravity fades on the way down");
        }
        /* collision queries */
        {
            v3 n; float pen;
            CHECK(cave_inside(c, c->path[3], 0.9f, &n, &pen) == 1 && pen == 0, "the tunnel axis is inside, clear of the walls");
            CHECK(cave_inside(c, v3_(0, 0, 0), 0.9f, &n, &pen) == 2 && pen == 0, "the core centre is inside the cavern");
            CHECK(cave_inside(c, v3_scale(c->entrance, b->radius + 20), 0.9f, &n, &pen) == 0, "space above the moon is outside");
            CHECK(cave_inside(c, v3_add(c->path[0], v3_scale(c->entrance, 1.0f)), 0.9f, &n, &pen) == 0, "above the shaft top is outside: the mouth is open");
            v3 u = v3_norm(v3_sub(c->path[4], c->path[3])), side = v3_any_perp(u);
            v3 near_wall = v3_add(c->path[3], v3_scale(side, c->tube_r - 0.3f));
            CHECK(cave_inside(c, near_wall, 0.9f, &n, &pen) == 1 && pen > 0.3f && pen < 0.9f, "near the tunnel wall you are pushed back by the shortfall");
            CHECK(v3_dot(n, side) < -0.9f, "the push points back toward the tunnel axis");
            v3 near_core = v3_scale(c->entrance, -(c->core_r - 0.3f));
            CHECK(cave_inside(c, near_core, 0.9f, &n, &pen) == 2 && pen > 0.55f, "near the cavern wall you are pushed inward");
            float s = cave_floor_below(c, c->path[3], v3_norm(c->path[3]), 5.0f);
            CHECK(s > 1.5f && s <= c->tube_r + 0.4f, "there is a floor a couple of units below the tunnel axis");
        }
        /* a player dropped into the tunnel lands on its floor and stays in the cave */
        {
            player p; memset(&p, 0, sizeof p);
            p.dom = moons[m];
            p.pos = v3_add(b->pos, c->path[4]);
            p.up = p.body_up = v3_norm(c->path[4]);
            p.facing = p.body_fwd = v3_any_perp(p.up);
            player_input in; memset(&in, 0, sizeof in);
            in.cam_forward = p.facing; in.view_forward = p.facing; in.view_up = p.up;
            for (int i = 0; i < 180; i++) player_update(&p, &w, &in, DT);
            CHECK(p.in_cave && !p.in_core, "the player is in the tunnel");
            CHECK(p.grounded, "and standing on its floor");
            v3 n; float pen;
            CHECK(cave_inside(c, v3_sub(p.pos, b->pos), 0.5f, &n, &pen) == 1, "still inside the tunnel walls after 3 s");
            CHECK(player_locked(&p, &w), "look is locked to the moon in the tunnel while standing");
            /* walking forward for a while stays inside the tunnel */
            in.my = 1;
            for (int i = 0; i < 300; i++) { player_update(&p, &w, &in, DT); in.cam_forward = p.facing; }
            CHECK(cave_inside(c, v3_sub(p.pos, b->pos), 0.3f, &n, &pen) == 1, "walking keeps the player inside the tunnel");
            /* the camera stays inside too */
            camera cam; camera_init(&cam, &p);
            camera_input cin; memset(&cin, 0, sizeof cin);
            for (int i = 0; i < 60; i++) camera_update(&cam, &w, &p, &cin, DT);
            CHECK(cave_inside(c, v3_sub(cam.pos, b->pos), 0.0f, &n, &pen) != 0, "the camera stays inside the cave");
        }
        /* the core: weightless, free look, floating */
        {
            player p; memset(&p, 0, sizeof p);
            p.dom = moons[m];
            p.pos = v3_add(b->pos, v3_scale(c->entrance, -c->core_r * 0.3f));
            p.up = p.body_up = v3_scale(c->entrance, -1.0f);
            p.facing = p.body_fwd = v3_any_perp(p.up);
            player_input in; memset(&in, 0, sizeof in);
            in.cam_forward = p.facing; in.view_forward = p.facing; in.view_up = p.up;
            v3 start = p.pos;
            for (int i = 0; i < 120; i++) player_update(&p, &w, &in, DT);
            CHECK(p.in_core && p.in_cave, "the player is in the core");
            CHECK(!p.grounded, "nothing to stand on in the core");
            CHECK(!player_locked(&p, &w), "free look in the core");
            CHECK(v3_len(v3_sub(p.pos, start)) < 0.3f, "weightless: the player hangs where they are (only the planet's faint tide tugs)");
        }
    }

    /* fireflies: only on the night side of a planet, only over land */
    {
        player p; memset(&p, 0, sizeof p);
        const body *b = &w.b[BODY_VERDANT];
        v3 sun = v3_norm(v3_sub(w.b[BODY_SUN].pos, b->pos));
        /* stand on land on the night side */
        v3 up = v3_scale(sun, -1.0f);
        for (int k = 0; k < 4000; k++) {
            float a = k * 0.37f, e = k * 0.11f;
            v3 d = v3_norm(v3_(cosf(a) * cosf(e), sinf(e), sinf(a) * cosf(e)));
            if (v3_dot(d, sun) < -0.4f && world_height_raw(&w, BODY_VERDANT, d) > b->sea_level + 2.0f) { up = d; break; }
        }
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
        v3 day = sun;
        for (int k = 0; k < 4000; k++) {
            float a = k * 0.37f, e = k * 0.11f;
            v3 d = v3_norm(v3_(cosf(a) * cosf(e), sinf(e), sinf(a) * cosf(e)));
            if (v3_dot(d, sun) > 0.4f && world_height_raw(&w, BODY_VERDANT, d) > b->sea_level + 2.0f) { day = d; break; }
        }
        p.up = p.body_up = day;
        p.pos = v3_add(b->pos, v3_scale(day, world_surface(&w, BODY_VERDANT, day) + PLAYER_HALF_HEIGHT));
        for (int i = 0; i < 300; i++) fireflies_update(&ff, &w, &p, DT);
        alive = 0;
        for (int i = 0; i < FIREFLY_N; i++) if (ff.f[i].alive) alive++;
        CHECK(alive == 0, "in daylight every firefly has faded out");
    }
    MT_END();
}
