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

    /* orbits: velocity matches finite difference, centripetal accel matches the parent's gravity */
    world_update(&w, DT);
    for (int i = 1; i < BODY_COUNT; i++) {
        v3 fd = v3_scale(v3_sub(w.b[i].pos, w.b[i].prev), 1.0f / DT);
        CHECK(v3_len(v3_sub(fd, w.b[i].vel)) < 0.05f, "body velocity matches its motion");
        v3 g = world_gravity_from(&w, w.b[i].parent, w.b[i].pos);
        float cent = w.b[i].orbit_r * w.b[i].omega * w.b[i].omega;
        CHECK_NEAR(v3_len(g), cent, 1e-3f, "orbit is consistent with the parent's gravity");
        CHECK_NEAR(v3_len(v3_sub(w.b[i].pos, w.b[w.b[i].parent].pos)), w.b[i].orbit_r, 1e-2f, "body sits at its orbit radius from its parent");
    }

    /* moons: a quarter the size of their planet, airless, cratered, and they own their surface */
    {
        int moons[3] = { BODY_CINDER, BODY_FERN, BODY_SLEET };
        for (int m = 0; m < 3; m++) {
            const body *b = &w.b[moons[m]], *par = &w.b[b->parent];
            CHECK(b->parent >= BODY_EMBER && b->parent <= BODY_FROST, "moon orbits a planet");
            CHECK_NEAR(b->radius / par->radius, 0.25f, 1e-3f, "moon is a quarter of its planet's radius");
            CHECK(b->atmo == 0, "moon has no atmosphere");
            CHECK(b->orbit_r - b->radius - b->amp > par->radius + par->atmo_h + 30.0f, "moon's inner edge clears the planet's atmosphere by 30+ units");
            CHECK(b->orbit_r < par->radius * 3.6f, "moon orbits fairly close to its planet");
            /* the moon's pull must dominate the tidal pull even on the planet-facing side */
            v3 tw = v3_norm(v3_sub(par->pos, b->pos));
            v3 hover = v3_add(b->pos, v3_scale(tw, b->radius + b->amp + 20.0f));
            v3 felt = v3_sub(world_gravity_at(&w, hover), world_frame_accel(&w, moons[m]));
            CHECK(v3_dot(felt, v3_scale(tw, -1.0f)) > 2.5f, "20 units above the planet-facing side the moon still pulls down firmly");
            float reach = b->orbit_r + b->radius + b->amp;
            CHECK(v3_len(par->pos) - reach > w.b[BODY_SUN].radius + 60.0f, "moon's orbit never reaches the sun");
            /* the planet-facing side is the worst case for owning the space above the surface */
            v3 towards = v3_norm(v3_sub(par->pos, b->pos));
            v3 near_surface = v3_add(b->pos, v3_scale(towards, b->radius + b->amp + 20.0f));
            CHECK(world_dominant(&w, near_surface) == moons[m], "moon owns the space 20 units above its planet-facing side");
            v3 far_out = v3_add(b->pos, v3_scale(towards, b->orbit_r * 0.6f));
            CHECK(world_dominant(&w, far_out) == b->parent, "well away from the moon its planet takes over");
            float lo = 1e9f, hi = -1e9f;
            for (int k = 0; k < 400; k++) {
                float a = k * 0.37f, e = k * 0.11f;
                v3 d = v3_norm(v3_(cosf(a) * cosf(e), sinf(e), sinf(a) * cosf(e)));
                float h = world_height_raw(&w, moons[m], d);
                if (h < lo) lo = h;
                if (h > hi) hi = h;
            }
            CHECK(lo < -0.6f * b->amp, "moon has crater floors well below the mean surface");
            CHECK(hi <= b->amp, "moon heights stay under the collision bound");
            CHECK(hi > 0.1f * b->amp, "moon has raised rims");
        }
        /* neighbouring moon paths never overlap, even when the planets line up */
        for (int m = 0; m + 1 < 3; m++) {
            const body *a = &w.b[moons[m]], *b2 = &w.b[moons[m + 1]];
            float ra = w.b[a->parent].orbit_r + a->orbit_r + a->radius + a->amp;
            float rb = w.b[b2->parent].orbit_r - b2->orbit_r - b2->radius - b2->amp;
            CHECK(rb - ra >= 15.0f, "neighbouring moon paths stay apart when the planets line up");
        }
        /* a body's frame accelerates with everything else's pull at its own centre, so a
           particle at the centre feels nothing: only tides remain */
        v3 fa = world_frame_accel(&w, BODY_VERDANT);
        v3 felt = v3_sub(v3_sub(world_gravity_at(&w, w.b[BODY_VERDANT].pos), world_gravity_from(&w, BODY_VERDANT, w.b[BODY_VERDANT].pos)), fa);
        CHECK(v3_len(felt) < 1e-4f, "at a planet's centre the frame cancels every other pull");
        v3 fm = world_frame_accel(&w, BODY_FERN);
        v3 felt_m = v3_sub(v3_sub(world_gravity_at(&w, w.b[BODY_FERN].pos), world_gravity_from(&w, BODY_FERN, w.b[BODY_FERN].pos)), fm);
        CHECK(v3_len(felt_m) < 1e-4f, "at a moon's centre too (the sun's pull is taken at the moon, not its planet)");
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

    /* the same on a moon, whose frame also swings around its planet; then a hop stays in the
       moon's frame and comes back down */
    {
        world_init(&w);
        player_spawn(&p, &w, BODY_FERN);
        player_input moon_in; memset(&moon_in, 0, sizeof moon_in); moon_in.cam_forward = v3_(1, 0, 0); moon_in.view_forward = v3_(1, 0, 0); moon_in.view_up = p.up;
        v3 m0 = v3_sub(p.pos, w.b[BODY_FERN].pos);
        for (int f = 0; f < 600; f++) { world_update(&w, DT); player_update(&p, &w, &moon_in, DT); }
        v3 m1 = v3_sub(p.pos, w.b[BODY_FERN].pos);
        CHECK(p.dom == BODY_FERN && p.grounded, "standing on the moon stays grounded on the moon");
        CHECK(v3_len(v3_sub(m0, m1)) < 1e-2f, "standing player rides the moon without drift");
        moon_in.jump_pressed = 1;
        int left_frame = 0, landed = 0;
        for (int f = 0; f < 900; f++) {
            world_update(&w, DT); player_update(&p, &w, &moon_in, DT); moon_in.jump_pressed = 0;
            if (p.dom != BODY_FERN) left_frame = 1;
            if (f > 10 && p.grounded) { landed = 1; break; }
        }
        CHECK(!left_frame, "a jump on the moon stays in the moon's frame");
        CHECK(landed, "and comes back down onto the moon");
        world_init(&w);
        player_spawn(&p, &w, BODY_START);
    }
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

    /* takeoff while looking at the ground: the body stays upright and the view eases level */
    {
        cin.look_y = -1.0f;
        for (int f = 0; f < 90; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
        cin.look_y = 0;
        CHECK(v3_dot(c.view, c.up) < -0.5f, "looking down at the ground before the jump");
        CHECK(v3_dot(p.body_up, p.up) > 0.99f, "standing body is upright");
        in.jump_pressed = 1; in.view_up = c.view_up; in.view_forward = c.view;
        float worst = 0, min_upright = 1;
        for (int f = 0; f < 60; f++) {
            v3 bu0 = p.body_up;
            world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT);
            in.jump_pressed = 0; in.view_up = c.view_up; in.view_forward = c.view;
            float step = acosf(fminf(1.0f, v3_dot(bu0, p.body_up)));
            if (step > worst) worst = step;
            float upright = v3_dot(p.body_up, p.up);
            if (upright < min_upright) min_upright = upright;
        }
        CHECK(!p.grounded, "airborne after the jump");
        CHECK(worst < 0.15f, "the body never turns more than about 8 degrees in one frame at takeoff");
        CHECK(min_upright > 0.8f, "the body stays close to upright through the takeoff");
        CHECK(player_locked(&p, &w), "a hop inside the atmosphere keeps the look locked to the planet");
        CHECK(v3_dot(c.view, c.up) < -0.3f, "so the view keeps its downward ground pitch through the hop");
        CHECK(fabsf(v3_dot(v3_cross(c.view, c.view_up), c.up)) < 0.05f, "and the horizon stays level");
        /* the landing must be just as smooth */
        worst = 0;
        for (int f = 0; f < 240 && !p.grounded; f++) {
            v3 bu0 = p.body_up;
            world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT);
            in.view_up = c.view_up; in.view_forward = c.view;
            float step = acosf(fminf(1.0f, v3_dot(bu0, p.body_up)));
            if (step > worst) worst = step;
        }
        CHECK(p.grounded, "back on the ground after the hop");
        CHECK(worst < 0.15f, "the body never snaps on landing either");
        for (int f = 0; f < 60; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    }

    /* flight: the pad pitches the view without limit, A boosts along it */
    in.jump_pressed = 1; in.thrust_up = 1; in.view_up = p.up;   /* constant thrust direction for the loop tests */
    world_update(&w, DT); player_update(&p, &w, &in, DT);
    in.jump_pressed = 0;
    CHECK(player_locked(&p, &w), "just off the ground the look is still locked to the planet");
    /* free look only unlocks above the atmosphere: lift the player straight out of it, then let
       the levelling finish before testing */
    p.pos = v3_add(p.pos, v3_scale(p.up, w.b[p.dom].atmo_h + 20.0f));
    for (int f = 0; f < 45; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(!player_locked(&p, &w), "above the atmosphere free look unlocks");
    cin.turn = 0; cin.look_y = 1.0f;
    v3 start_view = c.view;
    int looked_back = 0;
    /* the view still carries the downward pitch from the hop (landing keeps it now), so a full
       second of pad-up is needed to be clearly looking up */
    for (int f = 0; f < 60; f++) { world_update(&w, DT); player_update(&p, &w, &in, DT); camera_update(&c, &w, &p, &cin, DT); }
    CHECK(v3_dot(c.view, p.up) > 0.4f, "holding the pad up pitches the view up in flight");
    CHECK(fabsf(v3_dot(c.view, c.view_up)) < 1e-3f, "pitched view stays perpendicular to its up");
    for (int f = 0; f < 130; f++) {
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
        mesh gm[GRASS_CELLS];
        mesh_grass_cells(&w, BODY_START, gm);
        for (int g = 0; g < GRASS_CELLS; g++) { gtotal += gm[g].count; mesh_free(&gm[g]); }
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
