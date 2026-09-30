#include "player.h"

#define BODY_TURN_RATE 6.0f   /* how fast the model turns onto its target orientation, 1/s */
#define WALK_SPEED   8.0f
#define WADE_FACTOR  0.55f
#define SWIM_SPEED   3.5f
#define JUMP_SPEED   9.0f
#define THRUST      28.0f
#define BRAKE_RATE   2.5f
#define MAX_SPEED  150.0f
#define GROUND_SNAP  0.5f
#define SWIM_DEPTH   1.0f   /* centre this far below the surface: stop walking, start floating */
#define FLOAT_DEPTH  0.55f  /* where the centre settles while floating */

void player_spawn(player *p, const world *w, int bi) {
    /* stand where the sun is about 45 degrees above the horizon, on land */
    v3 up = v3_norm(v3_add(v3_norm(v3_scale(w->b[bi].pos, -1.0f)), v3_(0, 1, 0)));
    {
        const body *b = &w->b[bi];
        v3 axis = v3_any_perp(up), best = up;
        float best_h = world_height_raw(w, bi, up), want = b->sea_level + 0.2f * b->amp;
        for (int k = 1; k < 64 && best_h < want; k++) {
            v3 c = v3_rotate(up, axis, 0.08f * k);
            float h = world_height_raw(w, bi, c);
            if (h > best_h) { best_h = h; best = c; }
        }
        up = best;
    }
    p->dom = bi;
    p->up = up;
    p->pos = v3_add(w->b[bi].pos, v3_scale(up, world_surface(w, bi, up) + PLAYER_HALF_HEIGHT));
    p->vel = v3_(0, 0, 0);
    p->grounded = 1;
    p->swimming = 0;
    p->in_water = 0;
    p->thrusting = 0;
    p->jet_up = p->jet_down = p->jet_fwd = 0;
    p->jet_roll = 0;
    p->brake_dir = v3_(0, 0, 0);
    p->facing = v3_(1, 0, 0);
    p->body_up = p->up;
    p->body_fwd = p->facing;
    p->move_dir = v3_(0, 0, 0);
    p->speed = 0;
    p->altitude = 0;
    p->anim = 0;
}

static void switch_frame(player *p, const world *w, int to) {
    if (to == p->dom) return;
    /* keep the world velocity continuous: v_world = v_rel + v_body */
    p->vel = v3_add(p->vel, v3_sub(w->b[p->dom].vel, w->b[to].vel));
    p->dom = to;
}

void player_update(player *p, const world *w, const player_input *in, float dt) {
    switch_frame(p, w, world_dominant(w, p->pos));
    const body *dom = &w->b[p->dom];

    /* ride along with the dominant body */
    p->pos = v3_add(p->pos, v3_sub(dom->pos, dom->prev));
    p->up = v3_norm(v3_sub(p->pos, dom->pos));

    /* water: how deep is the centre of the body below the sea surface */
    float sea_r = world_sea_radius(w, p->dom);
    float dist_c = v3_len(v3_sub(p->pos, dom->pos));
    float depth = sea_r > 0 ? sea_r - dist_c : -1.0f;
    p->in_water = depth > -PLAYER_HALF_HEIGHT;
    if (depth > SWIM_DEPTH) { p->swimming = 1; p->grounded = 0; }
    else if (depth < 0.15f) p->swimming = 0;   /* well above the float depth: out of the water */

    /* camera-relative input in the tangent plane */
    v3 fwd = v3_tangent(in->cam_forward, p->up);
    fwd = v3_len(fwd) < 1e-4f ? v3_any_perp(p->up) : v3_norm(fwd);
    v3 right = v3_cross(fwd, p->up);
    v3 move = v3_add(v3_scale(right, in->mx), v3_scale(fwd, in->my));
    float mlen = v3_len(move);
    if (mlen > 1) { move = v3_scale(move, 1.0f / mlen); mlen = 1; }
    p->move_dir = mlen > 0.05f ? move : v3_(0, 0, 0);

    int jumped = 0;
    p->thrusting = 0;
    p->jet_up = p->jet_down = p->jet_fwd = 0;
    p->jet_roll = 0;
    p->brake_dir = v3_(0, 0, 0);
    if (p->swimming) {
        /* float at the surface: slow paddling, a spring holds the depth */
        v3 vt = v3_tangent(p->vel, p->up);
        vt = v3_lerp(vt, v3_scale(move, SWIM_SPEED), ease(5.0f, dt));
        float want = (depth - FLOAT_DEPTH) * 3.0f;   /* positive: rise */
        if (want > 4) want = 4;
        if (want < -4) want = -4;
        p->vel = v3_add(vt, v3_scale(p->up, want));
        if (in->jump_pressed) {
            p->vel = v3_add(p->vel, v3_scale(p->up, JUMP_SPEED * 0.9f));
            p->swimming = 0;
            jumped = 1;
        }
    } else if (p->grounded) {
        v3 vt = v3_tangent(p->vel, p->up);
        float speed = WALK_SPEED * (p->in_water ? WADE_FACTOR : 1.0f);
        vt = v3_lerp(vt, v3_scale(move, speed), ease(12.0f, dt));
        p->vel = vt;
        if (in->jump_pressed) {
            p->vel = v3_add(p->vel, v3_scale(p->up, JUMP_SPEED));
            p->grounded = 0;
            jumped = 1;
        }
    } else {
        /* gravity from everything, minus the sun's pull at the planet centre: the frame
           already accelerates that way because the orbit is circular */
        v3 g = world_gravity_at(w, p->pos);
        if (p->dom != BODY_SUN) g = v3_sub(g, world_gravity_from(w, BODY_SUN, dom->pos));
        p->vel = v3_add(p->vel, v3_scale(g, dt));
        v3 view = v3_norm(in->view_forward);
        v3 vup = v3_len(in->view_up) > 1e-4f ? v3_norm(in->view_up) : p->up;
        if (in->thrust_fwd) { p->vel = v3_add(p->vel, v3_scale(view, THRUST * dt)); p->jet_fwd = 1; }
        if (in->thrust_up) { p->vel = v3_add(p->vel, v3_scale(vup, THRUST * dt)); p->jet_up = 1; }
        if (in->thrust_down) { p->vel = v3_sub(p->vel, v3_scale(vup, THRUST * dt)); p->jet_down = 1; }
        p->jet_roll = in->roll;
        p->thrusting = p->jet_fwd || p->jet_up || p->jet_down || in->roll != 0;
        if (in->brake && v3_len(p->vel) > 0.5f) {
            p->vel = v3_scale(p->vel, 1.0f - ease(BRAKE_RATE, dt));
            p->brake_dir = v3_norm(p->vel);   /* exhaust goes the way we travel */
            p->thrusting = 1;
        }
        if (p->in_water) p->vel = v3_scale(p->vel, 1.0f - ease(2.0f, dt));   /* splashdown drag */
        /* in the air the model follows the camera heading */
        move = v3_(0, 0, 0); mlen = 0;
        p->move_dir = move;
        p->facing = fwd;
    }

    float sp = v3_len(p->vel);
    if (sp > MAX_SPEED) p->vel = v3_scale(p->vel, MAX_SPEED / sp);
    p->pos = v3_add(p->pos, v3_scale(p->vel, dt));

    /* surface contact */
    int touching = 0;
    for (int i = 0; i < BODY_COUNT; i++) {
        const body *b = &w->b[i];
        v3 d = v3_sub(p->pos, b->pos);
        float dist = v3_len(d);
        if (dist > b->radius + b->amp + PLAYER_HALF_HEIGHT + GROUND_SNAP) continue;   /* nowhere near */
        v3 n = v3_norm(d);
        float floor = world_surface(w, i, n) + PLAYER_HALF_HEIGHT;   /* terrain under our feet */
        int snap = (p->grounded && !jumped && i == p->dom && dist < floor + GROUND_SNAP);
        if (dist >= floor && !snap) continue;
        if (i == BODY_SUN) { player_spawn(p, w, BODY_START); return; }
        p->pos = v3_add(b->pos, v3_scale(n, floor));
        switch_frame(p, w, i);
        dom = &w->b[p->dom];
        p->up = n;
        float radial = v3_dot(p->vel, n);
        if (radial < 0 || snap) p->vel = v3_sub(p->vel, v3_scale(n, radial));
        touching = 1;
    }
    if (!jumped) p->grounded = touching;
    if (touching) p->swimming = 0;

    /* facing follows the input, stays tangent */
    if (mlen > 0.1f) p->facing = v3_lerp(p->facing, v3_norm(move), ease(10.0f, dt));
    p->facing = v3_tangent(p->facing, p->up);
    p->facing = v3_len(p->facing) < 1e-3f ? fwd : v3_norm(p->facing);

    p->speed = v3_len(p->vel);
    if (player_on_foot(p)) p->anim += p->speed * 1.6f * dt;   /* walk / paddle cycle */

    /* the body eases towards its target orientation rather than jumping to it */
    {
        v3 tu = p->up, tf = p->facing;
        if (!player_on_foot(p)) {
            if (v3_len(in->view_up) > 1e-4f) tu = v3_norm(in->view_up);
            if (v3_len(in->view_forward) > 1e-4f) tf = v3_norm(in->view_forward);
        }
        float k = ease(BODY_TURN_RATE, dt);
        v3 bu = v3_lerp(p->body_up, tu, k);
        p->body_up = v3_len(bu) < 1e-3f ? tu : v3_norm(bu);
        v3 bf = v3_tangent(v3_lerp(p->body_fwd, tf, k), p->body_up);
        p->body_fwd = v3_len(bf) < 1e-3f ? v3_any_perp(p->body_up) : v3_norm(bf);
    }
    p->altitude = v3_len(v3_sub(p->pos, dom->pos)) - world_surface(w, p->dom, p->up) - PLAYER_HALF_HEIGHT;
    if (p->grounded || fabsf(p->altitude) < 0.01f) p->altitude = 0;   /* no -0.0 flicker on the HUD */
}
