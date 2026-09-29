#include "player.h"

#define WALK_SPEED   5.5f
#define JUMP_SPEED   8.0f
#define THRUST      28.0f
#define BRAKE_RATE   2.5f
#define MAX_SPEED   80.0f
#define GROUND_SNAP  0.5f

void player_spawn(player *p, const world *w, int body) {
    /* stand where the sun is about 45 degrees above the horizon */
    v3 up = v3_norm(v3_add(v3_norm(v3_scale(w->b[body].pos, -1.0f)), v3_(0, 1, 0)));
    p->dom = body;
    p->up = up;
    p->pos = v3_add(w->b[body].pos, v3_scale(up, w->b[body].radius + PLAYER_HALF_HEIGHT));
    p->vel = v3_(0, 0, 0);
    p->grounded = 1;
    p->thrusting = 0;
    p->jet_up = p->jet_down = p->jet_fwd = 0;
    p->jet_roll = 0;
    p->facing = v3_(1, 0, 0);
    p->move_dir = v3_(0, 0, 0);
    p->speed = 0;
    p->altitude = 0;
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
    if (p->grounded) {
        v3 vt = v3_tangent(p->vel, p->up);
        vt = v3_lerp(vt, v3_scale(move, WALK_SPEED), ease(12.0f, dt));
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
        if (in->thrust_fwd) { p->vel = v3_add(p->vel, v3_scale(view, THRUST * dt)); p->jet_fwd = 1; }
        if (in->thrust_up) { p->vel = v3_add(p->vel, v3_scale(p->up, THRUST * dt)); p->jet_up = 1; }
        if (in->thrust_down) { p->vel = v3_sub(p->vel, v3_scale(p->up, THRUST * dt)); p->jet_down = 1; }
        p->jet_roll = in->roll;
        p->thrusting = p->jet_fwd || p->jet_up || p->jet_down || in->roll != 0;
        if (in->brake) p->vel = v3_scale(p->vel, 1.0f - ease(BRAKE_RATE, dt));
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
        float floor = b->radius + PLAYER_HALF_HEIGHT;
        int snap = (p->grounded && !jumped && i == p->dom && dist < floor + GROUND_SNAP);
        if (dist >= floor && !snap) continue;
        if (i == BODY_SUN) { player_spawn(p, w, BODY_START); return; }
        v3 n = v3_norm(d);
        p->pos = v3_add(b->pos, v3_scale(n, floor));
        switch_frame(p, w, i);
        dom = &w->b[p->dom];
        p->up = n;
        float radial = v3_dot(p->vel, n);
        if (radial < 0 || snap) p->vel = v3_sub(p->vel, v3_scale(n, radial));
        touching = 1;
    }
    if (!jumped) p->grounded = touching;

    /* facing follows the input, stays tangent */
    if (mlen > 0.1f) p->facing = v3_lerp(p->facing, v3_norm(move), ease(10.0f, dt));
    p->facing = v3_tangent(p->facing, p->up);
    p->facing = v3_len(p->facing) < 1e-3f ? fwd : v3_norm(p->facing);

    p->speed = v3_len(p->vel);
    p->altitude = v3_len(v3_sub(p->pos, dom->pos)) - dom->radius - PLAYER_HALF_HEIGHT;
    if (p->grounded || fabsf(p->altitude) < 0.01f) p->altitude = 0;   /* no -0.0 flicker on the HUD */
}
