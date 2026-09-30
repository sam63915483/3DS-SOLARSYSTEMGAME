#include "minitest.h"
#include "world.h"
#include "player.h"
#include "camera.h"
#include "mesh.h"
#include "noise.h"
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
    CHECK(world_dominant(&w, v3_(0, 0, 150)) == BODY_SUN, "sun dominates near the sun");

    /* standing still on a moving planet: no drift for ten seconds */
    player p; player_spawn(&p, &w, BODY_START);
    player_input in; memset(&in, 0, sizeof in); in.cam_forward = v3_(1, 0, 0); in.view_forward = v3_(1, 0, 0); in.view_up = p.up;
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
    CHECK(moved > 14.0f && moved < 18.0f, "walked about 16 units in two seconds");
    CHECK_NEAR(v3_len(off2), world_surface(&w, BODY_START, p.up) + PLAYER_HALF_HEIGHT, 1e-2f, "walking stays on the terrain surface");
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
    CHECK(max_alt > 1.0f && max_alt < 4.5f, "jump height is sane");

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
    p.pos = v3_(0, 0, 101.5f); p.vel = v3_(0, 0, -150);
    world_update(&w, DT); player_update(&p, &w, &in, DT);
    CHECK(p.dom == BODY_START && p.grounded, "touching the sun respawns on the start planet");

    /* landing on another planet: approach Ember at speed, get snapped to its surface */
    p.pos = v3_add(w.b[1].pos, v3_(0, w.b[1].radius + 2.0f, 0)); p.dom = 1; p.grounded = 0;
    p.vel = v3_(0, -10, 0);
    for (int f = 0; f < 60; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); }
    CHECK(p.grounded && p.dom == 1, "falling onto Ember lands on it");
    CHECK_NEAR(p.altitude, 0, 1e-3f, "landed at zero altitude");

    /* camera stays tangent and outside the planet */
    camera c; camera_init(&c, &p); camera_input cin; memset(&cin, 0, sizeof cin); cin.look_x = 1.0f;
    for (int f = 0; f < 120; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(fabsf(v3_dot(c.fwd, c.up)) < 1e-3f, "camera forward is perpendicular to camera up");
    CHECK(v3_len(v3_sub(c.pos, w.b[1].pos)) > w.b[1].radius, "camera is outside the planet");
    CHECK(v3_len(v3_sub(c.view, c.fwd)) < 1e-2f, "on the ground the view is level with the heading");

    /* pad up pitches the ground camera, walking keeps the pitch */
    cin.look_x = 0; cin.look_y = 1.0f;
    for (int f = 0; f < 60; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(v3_dot(c.view, c.up) > 0.5f, "pad up tilts the ground camera up");
    CHECK(p.grounded, "still grounded while looking around");
    cin.look_y = 0; in.my = 1;
    for (int f = 0; f < 90; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(v3_dot(c.view, c.up) > 0.4f, "walking keeps the look pitch");
    in.my = 0;
    for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }

    /* gyro look on the ground: yaw rate turns the heading left (counter-clockwise about up),
       pitch rate tilts the look and is clamped like the pad, and the horizon stays level */
    {
        camera_toggle_view(&c); camera_toggle_gyro(&c);
        CHECK(c.first_person && c.gyro, "gyro look can be switched on in first person");
        v3 fwd0 = c.fwd;
        cin.gyro_yaw = 1.0f;   /* rad/s, positive = turning left */
        for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
        CHECK(v3_dot(v3_cross(fwd0, c.fwd), c.up) > 0.3f, "gyro yaw turns the ground heading left");
        CHECK(fabsf(v3_dot(c.fwd, c.up)) < 1e-3f, "gyro yaw keeps the heading tangent to the ground");
        cin.gyro_yaw = 0; cin.gyro_pitch = -3.0f;
        for (int f = 0; f < 90; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
        CHECK(v3_dot(c.view, c.up) < -0.5f, "gyro pitch down tilts the ground look down");
        CHECK(v3_dot(c.view, c.up) > -0.95f, "gyro pitch is clamped on the ground");
        CHECK(fabsf(v3_dot(v3_cross(c.view, c.view_up), c.up)) < 0.05f, "gyro look keeps the horizon level on the ground");
        cin.gyro_pitch = 3.0f;
        for (int f = 0; f < 60; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
        cin.gyro_pitch = 0;
        camera_toggle_view(&c);
        CHECK(!c.first_person && !c.gyro, "leaving first person switches gyro look off");
        for (int f = 0; f < 60; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    }

    /* flight: the pad pitches the view without limit, A boosts along it */
    in.jump_pressed = 1; in.thrust_up = 1; in.view_up = p.up;   /* constant thrust direction for the loop tests */
    world_update(&w, DT); player_update(&p, &w, &in, DT);
    in.jump_pressed = 0;
    cin.turn = 0; cin.look_y = 1.0f;
    v3 start_view = c.view;
    int looked_back = 0;
    for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(v3_dot(c.view, p.up) > 0.4f, "holding the pad up pitches the view up in flight");
    CHECK(fabsf(v3_dot(c.view, c.view_up)) < 1e-3f, "pitched view stays perpendicular to its up");
    for (int f = 0; f < 160; f++) {
        world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT);
        if (v3_dot(c.view, start_view) < -0.9f) looked_back = 1;
    }
    CHECK(looked_back, "pitch is not clamped: a full backflip passes through looking backward");
    CHECK(v3_dot(c.view, start_view) > 0.9f, "after a full loop the view is back where it started");
    cin.look_y = 0;
    /* roll with the shoulder buttons keeps the view direction */
    v3 view_before_roll = c.view, up_before_roll = c.view_up;
    cin.turn = 1.0f;
    for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(v3_dot(c.view, view_before_roll) > 0.999f, "rolling keeps the view direction");
    CHECK(v3_dot(c.view_up, up_before_roll) < 0.7f, "rolling turns the camera up vector");
    cin.turn = 0;
    /* gyro in flight: pitch rate rotates the view toward its own up, yaw rate about that up */
    {
        v3 v0 = c.view, u0 = c.view_up;
        cin.gyro_pitch = 1.0f;
        for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
        CHECK(v3_dot(c.view, u0) > 0.3f, "gyro pitch up in flight tilts the view toward the camera up");
        cin.gyro_pitch = 0; cin.gyro_yaw = 1.0f;
        v3 v1 = c.view;
        for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
        CHECK(v3_dot(v3_cross(v1, c.view), c.view_up) > 0.3f, "gyro yaw in flight turns the view left about the camera up");
        CHECK(fabsf(v3_dot(c.view, c.view_up)) < 1e-3f, "gyro look keeps the flight basis orthonormal");
        cin.gyro_yaw = 0;
        (void)v0;
    }
    in.thrust_up = 0; in.thrust_fwd = 1; in.view_forward = c.view;
    v3 before = p.vel;
    for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); in.view_forward = c.view; }
    CHECK(v3_dot(v3_sub(p.vel, before), c.view) > 8.0f, "A boosts along the view direction");
    CHECK(p.thrusting, "flame shows while boosting");
    in.thrust_fwd = 0;

    /* up thrust follows the player's orientation, not gravity: pitch to face the planet,
       then X should push along the camera's up (tangent to the planet), not away from it */
    /* climb well clear of Ember first so nothing lands mid-test */
    if (p.grounded) in.jump_pressed = 1;
    in.thrust_up = 1; in.view_up = p.up;
    for (int f = 0; f < 150; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); in.jump_pressed = 0; in.view_up = p.up; }
    in.thrust_up = 0;
    CHECK(!p.grounded, "airborne before the orientation thrust test");
    c.view = v3_any_perp(p.up); c.view_up = p.up;   /* level the camera (the roll test banked it) */
    cin.look_y = -1.0f;
    for (int f = 0; f < 200; f++) {
        world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT);
        if (v3_dot(c.view, p.up) < -0.95f) break;
    }
    cin.look_y = 0;
    CHECK(v3_dot(c.view, p.up) < -0.9f, "pitched down to stare at the planet");
    in.thrust_up = 1; in.view_up = c.view_up; in.view_forward = c.view;
    before = p.vel;
    for (int f = 0; f < 30; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); in.view_up = c.view_up; in.view_forward = c.view; }
    v3 dv = v3_sub(p.vel, before);
    CHECK(v3_dot(dv, c.view_up) > 8.0f, "X thrusts along the player's own up while facing the planet");
    CHECK(fabsf(v3_dot(v3_norm(dv), p.up)) < 0.5f, "that thrust is mostly sideways to gravity, not away from the planet");
    in.thrust_up = 0;

    /* terrain: deterministic, sea is flat, land rises above the radius */
    {
        v3 d0 = v3_norm(v3_(0.3f, 0.8f, -0.5f));
        CHECK_NEAR(world_height(&w, BODY_START, d0), world_height(&w, BODY_START, d0), 0, "terrain height is deterministic");
        int sea = 0, land = 0, below = 0;
        for (int k = 0; k < 400; k++) {
            v3 d = v3_norm(v3_(noise_hashf(k, 0, 0, 5u) * 2 - 1, noise_hashf(k, 1, 0, 5u) * 2 - 1, noise_hashf(k, 2, 0, 5u) * 2 - 1));
            float h = world_height(&w, BODY_START, d);
            if (h == 0) sea++; else if (h > 0) land++; else below++;
        }
        (void)sea; (void)below;
        CHECK(land > 50, "Verdant has land");
        {
            int wet = 0, frost_below = 0;
            float sea_r = world_sea_radius(&w, BODY_START);
            for (int k = 0; k < 400; k++) {
                v3 d = v3_norm(v3_(noise_hashf(k, 0, 0, 5u) * 2 - 1, noise_hashf(k, 1, 0, 5u) * 2 - 1, noise_hashf(k, 2, 0, 5u) * 2 - 1));
                if (world_surface(&w, BODY_START, d) < sea_r - 0.5f) wet++;
                if (world_height(&w, 3, d) < w.b[3].sea_level - 1e-3f) frost_below++;
            }
            CHECK(wet > 50, "Verdant has real sea floor below the water surface");
            CHECK(frost_below == 0, "Frost ice sheet is flat: nothing below its level");
            v3 gd = world_grid_dir(&w, BODY_START, 10, 7);
            CHECK_NEAR(world_height(&w, BODY_START, gd), world_grid_height(&w, BODY_START, 10, 7), 1e-3f, "interpolated height matches the mesh vertex");
        }
        CHECK(world_height(&w, BODY_SUN, d0) == 0, "the sun is a perfect sphere");
    }

    /* planet mesh: indexed, winding faces outward, normals unit */
    mesh s = mesh_planet(&w, BODY_START);
    int bad = 0, badn = 0;
    for (int i = 0; i < s.idx_count; i += 3) {
        v3 a = v3_(s.v[s.idx[i]].pos[0], s.v[s.idx[i]].pos[1], s.v[s.idx[i]].pos[2]);
        v3 b = v3_(s.v[s.idx[i+1]].pos[0], s.v[s.idx[i+1]].pos[1], s.v[s.idx[i+1]].pos[2]);
        v3 cc = v3_(s.v[s.idx[i+2]].pos[0], s.v[s.idx[i+2]].pos[1], s.v[s.idx[i+2]].pos[2]);
        v3 n = v3_cross(v3_sub(b, a), v3_sub(cc, a));
        if (v3_len(n) > 1e-2f && v3_dot(n, a) < 0) bad++;
    }
    for (int k = 0; k < s.count; k++) {
        float l = sqrtf(s.v[k].nrm[0]*s.v[k].nrm[0] + s.v[k].nrm[1]*s.v[k].nrm[1] + s.v[k].nrm[2]*s.v[k].nrm[2]);
        if (fabsf(l - 1) > 1e-3f) badn++;
    }
    CHECK(bad == 0, "planet triangles wind counter-clockwise from outside");
    CHECK(badn == 0, "planet normals are unit length");
    CHECK(s.count <= 65535, "planet vertex count fits 16-bit indices");
    mesh_free(&s);
    {
        int total = 0, wet = 0;
        float sea_r = world_sea_radius(&w, BODY_START);
        for (int o = 0; o < 8; o++) {
            mesh pr = mesh_props(&w, BODY_START, o);
            total += pr.count;
            for (int k = 0; k < pr.count; k += 3) {
                v3 a = v3_(pr.v[k].pos[0], pr.v[k].pos[1], pr.v[k].pos[2]);
                if (v3_len(a) < sea_r - 1.5f) wet++;   /* sunk below the water */
            }
            mesh_free(&pr);
        }
        CHECK(total > 1000 && total % 3 == 0, "Verdant has batches of props");
        CHECK(wet == 0, "no props sit under the sea");
        int gtotal = 0;
        for (int g = 0; g < GRASS_CELLS; g++) { mesh gm = mesh_grass(&w, BODY_START, g); gtotal += gm.count; mesh_free(&gm); }
        CHECK(gtotal > 20000 && gtotal % 15 == 0, "Verdant has thousands of five-blade grass tufts");
        int etotal = 0;
        for (int o = 0; o < 8; o++) { mesh em = mesh_props(&w, 1, o); etotal += em.count; mesh_free(&em); }
        CHECK(etotal > 150 * 100 + 170 * 100, "Ember has trees as well as boulders");
    }

    /* swimming: drop into deep water, float, jump out */
    {
        v3 deep = v3_(0, 1, 0); float deepest = 1e9f;
        for (int k = 0; k < 2000; k++) {
            v3 d = v3_norm(v3_(noise_hashf(k, 0, 0, 9u) * 2 - 1, noise_hashf(k, 1, 0, 9u) * 2 - 1, noise_hashf(k, 2, 0, 9u) * 2 - 1));
            float h = world_height(&w, BODY_START, d);
            if (h < deepest) { deepest = h; deep = d; }
        }
        CHECK(deepest < w.b[BODY_START].sea_level - 3.0f, "found deep water on Verdant");
        player_spawn(&p, &w, BODY_START);
        p.pos = v3_add(w.b[BODY_START].pos, v3_scale(deep, world_sea_radius(&w, BODY_START) + 3.0f));
        p.grounded = 0; p.vel = v3_(0, 0, 0);
        memset(&in, 0, sizeof in); in.cam_forward = v3_any_perp(deep); in.view_forward = in.cam_forward; in.view_up = deep;
        for (int f = 0; f < 240; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); }
        CHECK(p.swimming, "sinks into deep water and floats");
        float depth = world_sea_radius(&w, BODY_START) - v3_len(v3_sub(p.pos, w.b[BODY_START].pos));
        CHECK(depth > 0.2f && depth < 1.2f, "floats with the body about half under");
        in.my = 1;
        v3 before_swim = p.pos;
        for (int f = 0; f < 60; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); }
        CHECK(v3_len(v3_sub(p.pos, before_swim)) > 2.0f, "A paddles forward while swimming");
        in.my = 0; in.jump_pressed = 1;
        world_update(&w, DT); player_update(&p, &w, &in, DT);
        in.jump_pressed = 0;
        CHECK(!p.swimming && !p.grounded, "X jumps out of the water");
    }
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
