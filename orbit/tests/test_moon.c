#include "minitest.h"
#include "world.h"
#include "moon.h"
#include "player.h"
#include "camera.h"
#include <string.h>
#include <time.h>

#define DT (1.0f / 60.0f)

/* A point in open air somewhere along a tunnel segment's axis. */
static v3 on_axis(const cave_seg *s, float t) { return v3_lerp(s->a, s->b, t); }

int main(void) {
    world w; world_init(&w);
    int moons[3] = { BODY_CINDER, BODY_FERN, BODY_SLEET };

    CHECK(!w.b[BODY_VERDANT].caves.enabled, "planets are not carved");
    for (int m = 0; m < 3; m++) {
        const body *b = &w.b[moons[m]];
        const cavenet *c = &b->caves;
        printf("  -- %s: %d mouths, %d tunnel segments, %d caverns\n", b->name, c->nmouth, c->nseg, c->nblob);
        CHECK(c->enabled, "every moon is carved");
        CHECK(c->nmouth >= 2, "at least two ways in");
        CHECK(c->nseg >= 30 && c->nseg <= CAVE_SEG_MAX, "a real network of tunnels");
        CHECK(c->nblob >= 4, "several caverns");
        /* every mouth's trunk reaches the core: some segment ends inside core_r + 2 */
        int reach = 0;
        for (int i = 0; i < c->nseg; i++) if (v3_len(c->seg[i].b) < c->core_r + 2.1f) reach++;
        CHECK(reach >= c->nmouth, "every trunk joins the core");
        /* walkable: no segment steeper than 32 degrees */
        int steep = 0;
        for (int i = 0; i < c->nseg; i++) {
            v3 d = v3_sub(c->seg[i].b, c->seg[i].a);
            float down = -v3_dot(v3_norm(d), v3_norm(c->seg[i].a));
            if (down > 0.56f) steep++;
        }
        CHECK(steep == 0, "no tunnel is steeper than 32 degrees");
        /* the field: air at the mouths, in the tunnels and the core; rock deep away from caves */
        {
            /* the trunk goes underground within its first few segments, and the ground is open there */
            int under = -1;
            for (int i = 0; i < 6 && under < 0; i++) if (moon_depth(&w, moons[m], c->seg[i].b) > 1.0f) under = i;
            CHECK(under >= 0 && under <= 3, "the tunnel is underground within three segments of the mouth");
            if (under >= 0) CHECK(moon_field(&w, moons[m], c->seg[under].b) < -1.0f, "and open air there: the mouth leads in");
            v3 sky = v3_scale(c->mouth[0], world_surface(&w, moons[m], c->mouth[0]) + 5.0f);
            CHECK(moon_field(&w, moons[m], sky) < -3.0f, "above the ground is air");
            int air = 0;
            for (int i = 0; i < c->nseg; i++) if (moon_field(&w, moons[m], on_axis(&c->seg[i], 0.5f)) < -1.5f) air++;
            CHECK(air == c->nseg, "every tunnel axis is open air");
            CHECK(moon_field(&w, moons[m], v3_(0, 0, 0)) < -c->core_r * 0.8f, "the core is hollow");
            /* far from every cave, the rock is solid */
            int solid = 0, tried = 0;
            for (int k = 0; k < 400; k++) {
                float a = k * 0.37f, e = k * 0.11f;
                v3 d = v3_norm(v3_(cosf(a) * cosf(e), sinf(e), sinf(a) * cosf(e)));
                v3 p = v3_scale(d, b->radius * 0.6f);
                float near = 1e9f;
                for (int i = 0; i < c->nseg; i++) { float dd = v3_len(v3_sub(p, v3_lerp(c->seg[i].a, c->seg[i].b, 0.5f))); if (dd < near) near = dd; }
                for (int i = 0; i < c->nblob; i++) { float dd = v3_len(v3_sub(p, c->blob[i].c)) - c->blob[i].r; if (dd < near) near = dd; }
                if (near < 12.0f) continue;
                tried++;
                if (moon_field(&w, moons[m], p) > 2.0f) solid++;
            }
            printf("     (%d of %d far-from-cave probes solid)\n", solid, tried);
            CHECK(tried > 3 && solid == tried, "rock away from the caves is solid");
            /* the surface far from any mouth is the terrain heightfield */
            v3 d = v3_scale(c->mouth[0], -1.0f);
            float r = world_surface(&w, moons[m], d);
            CHECK(fabsf(moon_field(&w, moons[m], v3_scale(d, r))) < 0.6f, "the field's surface matches the terrain height away from the caves");
            CHECK(moon_field(&w, moons[m], v3_scale(d, r - 1)) > 0.4f && moon_field(&w, moons[m], v3_scale(d, r + 1)) < -0.4f, "rock below it, air above it");
        }
        /* the mesh: dense, every vertex on the field's zero level, normals out of the rock */
        {
            clock_t t0 = clock();
            mesh oct[8];
            moon_build(&w, moons[m], oct);
            float secs = (float)(clock() - t0) / CLOCKS_PER_SEC;
            int total = 0, off = 0, flipped = 0;
            for (int o = 0; o < 8; o++) {
                total += oct[o].idx_count;
                for (int i = 0; i < oct[o].count; i++) {
                    v3 p = v3_(oct[o].v[i].pos[0], oct[o].v[i].pos[1], oct[o].v[i].pos[2]);
                    v3 n = v3_(oct[o].v[i].nrm[0], oct[o].v[i].nrm[1], oct[o].v[i].nrm[2]);
                    if (fabsf(moon_field(&w, moons[m], p)) > c->cell * 0.6f) off++;
                    if (v3_dot(n, moon_grad(&w, moons[m], p)) > 0) flipped++;
                }
                mesh_free(&oct[o]);
            }
            printf("     (%d triangles, meshed in %.2f s on this PC; %d off-surface, %d flipped)\n", total / 3, secs, off, flipped);
            CHECK(total / 3 > 3000, "the moon mesh has thousands of triangles");
            CHECK(off < total / 200, "vertices sit on the surface of the field");
            CHECK(flipped < total / 200, "normals point out of the rock");
            /* every triangle faces out of the rock: its winding normal disagrees with the gradient */
            int inward = 0, ntri = 0;
            {
                mesh oct2[8];
                moon_build(&w, moons[m], oct2);
                for (int o = 0; o < 8; o++) {
                    for (int t = 0; t + 2 < oct2[o].idx_count; t += 3) {
                        const vertex *a = &oct2[o].v[oct2[o].idx[t]], *bb = &oct2[o].v[oct2[o].idx[t + 1]], *cc = &oct2[o].v[oct2[o].idx[t + 2]];
                        v3 pa = v3_(a->pos[0], a->pos[1], a->pos[2]), pb = v3_(bb->pos[0], bb->pos[1], bb->pos[2]), pc = v3_(cc->pos[0], cc->pos[1], cc->pos[2]);
                        v3 face = v3_cross(v3_sub(pb, pa), v3_sub(pc, pa));
                        v3 centre = v3_scale(v3_add(v3_add(pa, pb), pc), 1.0f / 3.0f);
                        if (v3_dot(face, moon_grad_wide(&w, moons[m], centre, 0.5f)) > 0) inward++;
                        ntri++;
                    }
                    mesh_free(&oct2[o]);
                }
            }
            printf("     (%d of %d triangles wound inward)\n", inward, ntri);
            CHECK(inward < ntri / 100, "triangles are wound to face out of the rock (no culled holes)");
            mesh gm = moon_crystals(&w, moons[m]);
            CHECK(gm.count >= 24 * 8, "crystals and the orb exist");
            mesh_free(&gm);
        }
        /* gravity: normal outside, zero in the core */
        CHECK(v3_len(world_gravity_from(&w, moons[m], v3_add(b->pos, v3_scale(c->mouth[0], b->radius + 5)))) > 5.0f, "gravity is normal above the surface");
        CHECK_NEAR(v3_len(world_gravity_from(&w, moons[m], v3_add(b->pos, v3_scale(c->mouth[0], c->core_r * 0.5f)))), 0.0f, 1e-4f, "gravity is zero inside the core");
        /* a player dropped into a tunnel lands on its floor and can walk along it */
        {
            player p; memset(&p, 0, sizeof p);
            p.dom = moons[m];
            int si = 0;   /* a segment 8-20 units under the surface, where gravity is still strong */
            for (int i = 0; i < c->nseg; i++) { float dp = moon_depth(&w, moons[m], on_axis(&c->seg[i], 0.5f)); if (dp > 8 && dp < 20) { si = i; break; } }
            v3 start = on_axis(&c->seg[si], 0.5f);
            p.pos = v3_add(b->pos, start);
            p.up = p.body_up = v3_norm(start);
            v3 axis = v3_norm(v3_sub(c->seg[si].b, c->seg[si].a));
            p.facing = p.body_fwd = v3_norm(v3_tangent(axis, p.up));
            player_input in; memset(&in, 0, sizeof in);
            in.cam_forward = p.facing; in.view_forward = p.facing; in.view_up = p.up;
            for (int i = 0; i < 180; i++) player_update(&p, &w, &in, DT);
            CHECK(p.in_cave && !p.in_core, "the player is underground in a tunnel");
            CHECK(p.grounded, "and standing on its floor");
            float f = moon_field(&w, moons[m], v3_sub(p.pos, b->pos));
            CHECK(f < 0 && f > -PLAYER_HALF_HEIGHT - 0.6f, "feet on the rock, head in the air");
            in.my = 1;
            int stuck = 0; v3 last = p.pos, walk_start = p.pos;
            for (int i = 0; i < 300; i++) {
                player_update(&p, &w, &in, DT);
                in.cam_forward = in.view_forward = p.facing; in.view_up = p.up;
                if (i % 60 == 59) { if (v3_len(v3_sub(p.pos, last)) < 0.5f) stuck++; last = p.pos; }
            }
            CHECK(moon_field(&w, moons[m], v3_sub(p.pos, b->pos)) < 0, "after walking 5 s the player is still in open air, not in rock");
            CHECK(stuck == 0, "and never got stuck");
            CHECK(v3_len(v3_sub(p.pos, walk_start)) > 12.0f, "walking 5 s along a sloping, bending tunnel covers real ground");
            camera cam; camera_init(&cam, &p);
            camera_input cin; memset(&cin, 0, sizeof cin);
            for (int i = 0; i < 90; i++) camera_update(&cam, &w, &p, &cin, DT);
            CHECK(moon_field(&w, moons[m], v3_sub(cam.pos, b->pos)) < 0, "the camera is in the air, not in the rock");
        }
        /* the core: weightless, free look, floating, and thrust moves you */
        {
            player p; memset(&p, 0, sizeof p);
            p.dom = moons[m];
            p.pos = v3_add(b->pos, v3_scale(c->mouth[0], -c->core_r * 0.3f));
            p.up = p.body_up = v3_scale(c->mouth[0], -1.0f);
            p.facing = p.body_fwd = v3_any_perp(p.up);
            player_input in; memset(&in, 0, sizeof in);
            in.cam_forward = p.facing; in.view_forward = p.facing; in.view_up = p.up;
            v3 start = p.pos;
            for (int i = 0; i < 120; i++) player_update(&p, &w, &in, DT);
            CHECK(p.in_core && p.in_cave, "the player is in the core");
            CHECK(!p.grounded && !player_locked(&p, &w), "floating with free look");
            CHECK(v3_len(v3_sub(p.pos, start)) < 0.3f, "weightless: hangs where they are");
            in.thrust_fwd = 1;
            for (int i = 0; i < 60; i++) player_update(&p, &w, &in, DT);
            CHECK(v3_dot(v3_sub(p.pos, start), p.facing) > 2.0f, "a second of thrust flies you forward");
            in.thrust_fwd = 0;
            for (int i = 0; i < 600; i++) player_update(&p, &w, &in, DT);   /* coast into the wall */
            CHECK(moon_field(&w, moons[m], v3_sub(p.pos, b->pos)) < 0.2f, "coasting into the cavern wall stops you at it, not inside it");
        }
        /* a hop on a moon keeps the look locked; well above it, free look */
        {
            player p; memset(&p, 0, sizeof p);
            p.dom = moons[m]; p.grounded = 0; p.altitude = 5;
            CHECK(player_locked(&p, &w), "5 up off a moon the look is still locked");
            p.altitude = 20;
            CHECK(!player_locked(&p, &w), "20 up it is free");
        }
        /* a player standing on the surface away from the caves stays on the terrain */
        {
            player p; memset(&p, 0, sizeof p);
            p.dom = moons[m];
            v3 d = v3_scale(c->mouth[0], -1.0f);
            p.up = p.body_up = d;
            p.pos = v3_add(b->pos, v3_scale(d, world_surface(&w, moons[m], d) + PLAYER_HALF_HEIGHT + 3.0f));
            p.facing = p.body_fwd = v3_any_perp(d);
            player_input in; memset(&in, 0, sizeof in);
            in.cam_forward = p.facing; in.view_forward = p.facing; in.view_up = p.up;
            for (int i = 0; i < 180; i++) player_update(&p, &w, &in, DT);
            CHECK(p.grounded && !p.in_cave, "drops onto the surface and stands");
            CHECK_NEAR(v3_len(v3_sub(p.pos, b->pos)) - PLAYER_HALF_HEIGHT, world_surface(&w, moons[m], v3_norm(v3_sub(p.pos, b->pos))), 0.6f, "at the terrain height");
        }
    }
    MT_END();
}
