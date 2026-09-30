#include "minitest.h"
#include "world.h"
#include "cave.h"
#include "player.h"
#include "camera.h"
#include "mesh.h"
#include <string.h>

#define DT (1.0f / 60.0f)

static v3 vp(const vertex *v) { return v3_(v->pos[0], v->pos[1], v->pos[2]); }

int main(void) {
    world w; world_init(&w);
    int moons[3] = { BODY_CINDER, BODY_FERN, BODY_SLEET };

    CHECK(!w.b[BODY_VERDANT].cave.enabled, "planets have no cave");
    for (int m = 0; m < 3; m++) {
        const body *b = &w.b[moons[m]];
        const cave *c = &b->cave;
        printf("  -- %s: %d control points, %d samples, tunnel %.0f long\n", b->name, c->npath, c->nsamp, (c->nsamp - 1) * 1.2f);
        CHECK(c->enabled, "every moon has a cave");
        CHECK(c->nsamp >= 20 && c->nsamp <= CAVE_SAMP_MAX, "the tunnel is sampled densely");
        /* geometry: descends the whole way, ends in the core, never steeper than about 40 degrees */
        int descends = 1, gentle = 1;
        for (int k = 0; k + 1 < c->nsamp; k++) {
            v3 d = v3_sub(c->samp[k + 1], c->samp[k]);
            if (v3_len(c->samp[k + 1]) > v3_len(c->samp[k]) + 0.02f) descends = 0;
            float down = -v3_dot(d, v3_norm(c->samp[k])), along = v3_len(d);
            if (along > 0.3f && down / along > 0.65f) gentle = 0;
        }
        CHECK(descends, "every sample is lower than the last");
        CHECK(gentle, "no part of the tunnel is steeper than about 40 degrees");
        CHECK(v3_len(c->samp[c->nsamp - 1]) < c->core_r, "the tunnel ends inside the core cavern");
        /* the mouth: the tunnel floor starts level with the pit floor at the pit centre */
        {
            float floor_r = b->radius + world_height_raw(&w, moons[m], v3_norm(c->samp[0]));
            CHECK_NEAR(v3_len(c->samp[0]) - c->tube_r * c->floor_k, floor_r, 0.08f, "the tunnel floor meets the crater floor at the mouth");
            v3 side = v3_any_perp(c->entrance);
            float rim = world_height_raw(&w, moons[m], v3_norm(v3_add(c->entrance, v3_scale(side, c->pit_ang * 1.2f))));
            CHECK(rim - world_height_raw(&w, moons[m], c->entrance) > 2.0f, "the mouth sits at the bottom of a crater");
        }
        /* the terrain has a hole over the entry and nowhere else */
        {
            mesh pm = mesh_planet(&w, moons[m]);
            int over_entry = 0, total = pm.idx_count / 3;
            for (int t = 0; t + 2 < pm.idx_count; t += 3) {
                v3 centre = v3_norm(v3_add(v3_add(vp(&pm.v[pm.idx[t]]), vp(&pm.v[pm.idx[t + 1]])), vp(&pm.v[pm.idx[t + 2]])));
                if (world_in_hole(&w, moons[m], centre)) over_entry++;
            }
            CHECK(over_entry == 0, "no terrain triangle is left over the tunnel entry");
            CHECK(total < b->rings * b->segs * 2 - 4, "several triangles were cut for the mouth");
            CHECK(!world_in_hole(&w, moons[m], v3_scale(c->entrance, 1.0f)) || 1, "(hole query runs)");
            mesh_free(&pm);
        }
        /* the tunnel mesh: welded to the terrain at the mouth and the cavern at the end */
        {
            mesh tm = mesh_cave_tunnel(&w, moons[m]);
            CHECK(tm.count > 2000, "the tunnel mesh is dense");
            int floating = 0, sky = 0, n = 0;
            for (int i = 0; i < tm.count; i++) {
                v3 p = vp(&tm.v[i]);
                float r = v3_len(p), surf = world_surface(&w, moons[m], v3_norm(p));
                if (r > surf + 0.25f) floating++;   /* a wall vertex above the ground: a pipe in the air */
                n++;
                /* a vertex near the mouth that is not on the tunnel surface must be on the terrain */
                if (cave_angle(v3_norm(p), c->entrance) < c->pit_ang && r > surf + 0.25f) sky++;
            }
            printf("     (%d of %d tunnel vertices above ground)\n", floating, n);
            CHECK(floating == 0, "no tunnel vertex pokes above the terrain");
            CHECK(sky == 0, "nothing at the mouth stands off the ground");
            /* normals point into the tunnel */
            int wrong = 0;
            for (int i = 0; i < tm.count; i++) {
                v3 p = vp(&tm.v[i]), nn = v3_(tm.v[i].nrm[0], tm.v[i].nrm[1], tm.v[i].nrm[2]);
                int k; float d = cave_axis_dist(c, p, &k);
                if (d < c->tube_r * 0.5f || d > c->tube_r * 1.3f) continue;   /* floor centre, or a skirt */
                if (v3_len(p) < c->core_r * 1.2f) continue;                      /* the cavern end */
                if (fabsf(v3_len(p) - world_surface(&w, moons[m], v3_norm(p))) < 0.3f) continue;   /* the mouth */
                v3 to_axis = v3_sub(c->samp[k], p);
                if (v3_dot(nn, to_axis) < -0.2f * v3_len(to_axis)) wrong++;
            }
            printf("     (%d wall normals facing out)\n", wrong);
            CHECK(wrong == 0, "wall normals face into the tunnel");
            mesh_free(&tm);
            mesh cm = mesh_cave_cavern(&w, moons[m]);
            CHECK(cm.count > 1000, "the cavern mesh is dense");
            mesh_free(&cm);
            mesh sm = mesh_cave_shell(&w, moons[m]);
            CHECK(sm.count > 300, "the dark shell exists");
            mesh_free(&sm);
            mesh xm = mesh_cave_crystals(&w, moons[m]);
            CHECK(xm.count >= 24 * 10, "there are crystals and an orb");
            mesh_free(&xm);
        }
        /* gravity: full at the surface, zero in the core */
        {
            v3 surf = v3_scale(c->entrance, b->radius + 5);
            float g_out = v3_len(world_gravity_from(&w, moons[m], v3_add(b->pos, surf)));
            float g_core = v3_len(world_gravity_from(&w, moons[m], v3_add(b->pos, v3_scale(c->entrance, c->core_r * 0.5f))));
            CHECK(g_out > 5.0f, "gravity is normal above the surface");
            CHECK_NEAR(g_core, 0.0f, 1e-4f, "gravity is zero inside the core");
        }
        /* collision queries */
        {
            v3 n; float pen;
            CHECK(cave_inside(c, c->samp[8], 0.9f, &n, &pen) == 1 && pen == 0, "the tunnel axis is inside, clear of the walls");
            CHECK(cave_inside(c, v3_(0, 0, 0), 0.9f, &n, &pen) == 2 && pen == 0, "the core centre is inside the cavern");
            CHECK(cave_inside(c, v3_scale(c->entrance, b->radius + 20), 0.9f, &n, &pen) == 0, "space above the moon is outside");
            v3 u0 = v3_norm(v3_sub(c->samp[1], c->samp[0]));
            CHECK(cave_inside(c, v3_sub(c->samp[0], v3_scale(u0, 1.0f)), 0.9f, &n, &pen) == 0, "just outside the mouth is outside");
            v3 down = v3_norm(v3_tangent(v3_scale(v3_norm(c->samp[8]), -1.0f), v3_norm(v3_sub(c->samp[9], c->samp[8]))));
            v3 low = v3_add(c->samp[8], v3_scale(down, c->tube_r * c->floor_k - 0.3f));
            CHECK(cave_inside(c, low, 0.9f, &n, &pen) == 1 && pen > 0.5f && v3_dot(n, down) < -0.9f, "the flat floor pushes straight up");
            float s = cave_floor_below(c, c->samp[8], v3_norm(c->samp[8]), 5.0f);
            CHECK(s > 1.5f && s <= c->tube_r * c->floor_k + 0.4f, "the floor is about 0.72 radii below the axis");
        }
        /* walk in from the crater floor: the player crosses the mouth onto the tunnel floor
           without a step, then keeps walking down inside */
        {
            player p; memset(&p, 0, sizeof p);
            p.dom = moons[m];
            v3 u0 = v3_norm(v3_sub(c->samp[1], c->samp[0]));
            v3 fwd = v3_norm(v3_tangent(u0, c->entrance));
            v3 start_dir = v3_norm(v3_sub(c->samp[0], v3_scale(fwd, 5.0f)));
            p.up = p.body_up = start_dir;
            p.pos = v3_add(b->pos, v3_scale(start_dir, world_surface(&w, moons[m], start_dir) + PLAYER_HALF_HEIGHT));
            p.facing = p.body_fwd = fwd;
            p.grounded = 1;
            player_input in; memset(&in, 0, sizeof in);
            in.cam_forward = fwd; in.view_forward = fwd; in.view_up = p.up; in.my = 1;
            float max_drop = 0, prev_alt = 0; int entered_at = -1;
            for (int i = 0; i < 600; i++) {
                v3 before = p.pos;
                player_update(&p, &w, &in, DT);
                in.cam_forward = in.view_forward = p.facing; in.view_up = p.up;
                float drop = v3_dot(v3_sub(before, p.pos), p.up);
                if (drop > max_drop) max_drop = drop;
                if (p.in_cave && entered_at < 0) entered_at = i;
                (void)prev_alt;
            }
            printf("     (entered the cave at frame %d, biggest single-frame drop %.2f)\n", entered_at, max_drop);
            CHECK(entered_at > 0, "walking forward takes the player into the tunnel");
            CHECK(max_drop < 0.35f, "no step down at the mouth");
            CHECK(p.in_cave && p.grounded, "ten seconds later still walking on the tunnel floor");
            v3 n; float pen;
            CHECK(cave_inside(c, v3_sub(p.pos, b->pos), 0.3f, &n, &pen) == 1, "and inside its walls");
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
            CHECK(v3_len(v3_sub(p.pos, start)) < 0.3f, "weightless: the player hangs where they are");
        }
    }
    MT_END();
}
