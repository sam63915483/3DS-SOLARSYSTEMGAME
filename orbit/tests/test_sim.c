#include "minitest.h"
#include "world.h"
#include "player.h"
#include "camera.h"
#include "mesh.h"
#include <string.h>

#define DT (1.0f / 60.0f)

static v3 world_vel(const player *p, const world *w) { return v3_add(p->vel, w->b[p->dom].vel); }

int main(void) {
    world w; world_init(&w);

    /* orbits: velocity matches finite difference, centripetal accel matches sun gravity */
    world_update(&w, DT);
    for (int i = 1; i < BODY_COUNT; i++) {
        v3 fd = v3_scale(v3_sub(w.b[i].pos, w.b[i].prev), 1.0f / DT);
        CHECK(v3_len(v3_sub(fd, w.b[i].vel)) < 0.05f, "body velocity matches its motion");
        v3 g = world_gravity_from(&w, BODY_SUN, w.b[i].pos);
        float cent = w.b[i].orbit_r * w.b[i].omega * w.b[i].omega;
        CHECK_NEAR(v3_len(g), cent, 1e-3f, "orbit is consistent with the sun's gravity");
    }

    /* gravity above a planet points at it and beats the sun there */
    world_init(&w);
    v3 above = v3_add(w.b[BODY_START].pos, v3_(0, 30, 0));
    v3 g = world_gravity_at(&w, above);
    CHECK(g.y < 0, "gravity above the start planet points down");
    CHECK(world_dominant(&w, above) == BODY_START, "start planet dominates near its surface");
    CHECK(world_dominant(&w, v3_(0, 0, 100)) == BODY_SUN, "sun dominates deep space");

    /* standing still on a moving planet: no drift for ten seconds */
    player p; player_spawn(&p, &w, BODY_START);
    player_input in; memset(&in, 0, sizeof in); in.cam_forward = v3_(1, 0, 0); in.view_forward = v3_(1, 0, 0);
    v3 off0 = v3_sub(p.pos, w.b[BODY_START].pos);
    for (int f = 0; f < 600; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); }
    v3 off1 = v3_sub(p.pos, w.b[BODY_START].pos);
    CHECK(v3_len(v3_sub(off0, off1)) < 1e-2f, "standing player rides the planet without drift");
    CHECK(p.grounded, "still grounded after ten seconds");
    CHECK(p.speed < 1e-3f, "no relative speed while standing");

    /* walking moves along the surface and stays on it */
    in.my = 1;
    for (int f = 0; f < 120; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); }
    v3 off2 = v3_sub(p.pos, w.b[BODY_START].pos);
    float moved = v3_len(v3_sub(off2, off1));
    CHECK(moved > 6.0f && moved < 9.0f, "walked about 8 units in two seconds");
    CHECK_NEAR(v3_len(off2), w.b[BODY_START].radius + PLAYER_HALF_HEIGHT, 1e-2f, "walking stays on the surface");
    CHECK(v3_dot(p.facing, p.up) < 1e-3f, "facing is tangent to the surface");
    in.my = 0;

    /* jump: leaves the ground, comes back, grounded again */
    in.jump_pressed = 1;
    world_update(&w, DT); player_update(&p, &w, &in, DT);
    in.jump_pressed = 0;
    CHECK(!p.grounded, "jump leaves the ground");
    float max_alt = 0; int landed = 0;
    for (int f = 0; f < 600 && !landed; f++) {
        world_update(&w, DT); player_update(&p, &w, &in, DT);
        if (p.altitude > max_alt) max_alt = p.altitude;
        landed = p.grounded;
    }
    CHECK(landed, "jump lands again");
    CHECK(max_alt > 1.0f && max_alt < 3.5f, "jump height is sane");

    /* jetpack: holding B escapes the planet and the frame switches to the sun without a
       jump in world velocity */
    in.jump_pressed = 1; in.thrust_up = 1;
    int switched = 0; float worst_jump = 0;
    for (int f = 0; f < 60 * 60 && !switched; f++) {
        world_update(&w, DT);
        int before = p.dom; v3 vw_before = world_vel(&p, &w);
        player_update(&p, &w, &in, DT);
        in.jump_pressed = 0;
        if (p.dom != before) {
            /* one frame of gravity + thrust is the only allowed change */
            float d = v3_len(v3_sub(world_vel(&p, &w), vw_before));
            if (d > worst_jump) worst_jump = d;
            switched = 1;
        }
    }
    CHECK(switched, "jetpack escapes to the sun's sphere of influence");
    CHECK(worst_jump < 1.0f, "world velocity is continuous across the frame switch");
    CHECK(p.dom == BODY_SUN, "dominant body is the sun after escaping");
    in.thrust_up = 0;

    /* respawn when hitting the sun */
    p.pos = v3_(0, 0, 41.5f); p.vel = v3_(0, 0, -60);
    world_update(&w, DT); player_update(&p, &w, &in, DT);
    CHECK(p.dom == BODY_START && p.grounded, "touching the sun respawns on the start planet");

    /* landing on another planet: approach Ember at speed, get snapped to its surface */
    p.pos = v3_add(w.b[1].pos, v3_(0, 12.0f, 0)); p.dom = 1; p.grounded = 0;
    p.vel = v3_(0, -10, 0);
    for (int f = 0; f < 60; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); }
    CHECK(p.grounded && p.dom == 1, "falling onto Ember lands on it");
    CHECK_NEAR(p.altitude, 0, 1e-3f, "landed at zero altitude");

    /* camera stays tangent and outside the planet */
    camera c; camera_init(&c, &p); camera_input cin; memset(&cin, 0, sizeof cin); cin.turn = 1.0f;
    for (int f = 0; f < 120; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(fabsf(v3_dot(c.fwd, c.up)) < 1e-3f, "camera forward is perpendicular to camera up");
    CHECK(v3_len(v3_sub(c.pos, w.b[1].pos)) > w.b[1].radius, "camera is outside the planet");
    CHECK(fabsf(c.pitch) < 1e-3f, "pitch stays zero on the ground");

    /* flight: pad pitches the view, B thrusts along it */
    in.jump_pressed = 1;
    world_update(&w, DT); player_update(&p, &w, &in, DT);
    in.jump_pressed = 0;
    cin.turn = 0; cin.look_y = 1.0f;
    for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(c.pitch > 0.5f, "holding the pad up pitches the view up in flight");
    CHECK(fabsf(v3_dot(c.view, c.view_up)) < 1e-3f, "pitched view stays perpendicular to its up");
    CHECK(v3_dot(c.view, p.up) > 0.4f, "pitched view points partly away from the planet");
    cin.look_y = 0; in.thrust_fwd = 1; in.view_forward = c.view;
    v3 before = p.vel;
    for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); in.view_forward = c.view; }
    CHECK(v3_dot(v3_sub(p.vel, before), c.view) > 8.0f, "B thrusts along the view direction");
    CHECK(p.thrusting, "flame shows while thrusting forward");
    in.thrust_fwd = 0;

    /* meshes: sphere normals are unit and winding faces outward */
    mesh s = mesh_sphere(5, 8, 12, 0xFF0000FF, 0x00FF00FF);
    int bad = 0;
    for (int i = 0; i < s.count; i += 3) {
        v3 a = v3_(s.v[i].pos[0], s.v[i].pos[1], s.v[i].pos[2]);
        v3 b = v3_(s.v[i+1].pos[0], s.v[i+1].pos[1], s.v[i+1].pos[2]);
        v3 cc = v3_(s.v[i+2].pos[0], s.v[i+2].pos[1], s.v[i+2].pos[2]);
        v3 n = v3_cross(v3_sub(b, a), v3_sub(cc, a));
        if (v3_len(n) > 1e-6f && v3_dot(n, a) < 0) bad++;
    }
    CHECK(bad == 0, "sphere triangles wind counter-clockwise from outside");
    CHECK(s.v[0].col[0] == 0 && s.v[0].col[1] == 0xFF, "vertex colour bytes are r g b a");
    mesh_free(&s);
    mesh bx = mesh_box(1, 2, 3, 0xFFFFFFFF);
    bad = 0;
    for (int i = 0; i < bx.count; i += 3) {
        v3 a = v3_(bx.v[i].pos[0], bx.v[i].pos[1], bx.v[i].pos[2]);
        v3 b = v3_(bx.v[i+1].pos[0], bx.v[i+1].pos[1], bx.v[i+1].pos[2]);
        v3 cc = v3_(bx.v[i+2].pos[0], bx.v[i+2].pos[1], bx.v[i+2].pos[2]);
        v3 n = v3_cross(v3_sub(b, a), v3_sub(cc, a));
        v3 fn = v3_(bx.v[i].nrm[0], bx.v[i].nrm[1], bx.v[i].nrm[2]);
        if (v3_dot(n, fn) <= 0) bad++;
    }
    CHECK(bad == 0, "box triangles wind counter-clockwise from outside");
    mesh_free(&bx);

    MT_END();
}
