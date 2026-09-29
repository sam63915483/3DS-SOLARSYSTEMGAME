#include "camera.h"

#define CAM_BACK     7.0f
#define CAM_UP       2.2f
#define TURN_RATE    2.0f   /* shoulder buttons, rad/s */
#define LOOK_YAW     2.2f   /* circle pad in flight, rad/s */
#define LOOK_PITCH   1.8f
#define PITCH_LIMIT  1.35f  /* about 77 degrees */

static void finish(camera *c, const world *w, const player *p) {
    c->fwd = v3_tangent(c->fwd, c->up);
    c->fwd = v3_len(c->fwd) < 1e-3f ? v3_any_perp(c->up) : v3_norm(c->fwd);

    /* pitch the heading around the camera's right axis */
    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    c->view = v3_norm(v3_add(v3_scale(c->fwd, cp), v3_scale(c->up, sp)));
    c->view_up = v3_norm(v3_add(v3_scale(c->up, cp), v3_scale(c->fwd, -sp)));

    c->pos = v3_add(v3_add(p->pos, v3_scale(c->view_up, CAM_UP)), v3_scale(c->view, -CAM_BACK));
    c->target = v3_add(p->pos, v3_scale(c->view_up, 1.0f));

    /* keep the camera out of the ground */
    const body *b = &w->b[p->dom];
    v3 d = v3_sub(c->pos, b->pos);
    float dist = v3_len(d), min = b->radius + 0.6f;
    if (dist < min) c->pos = v3_add(b->pos, v3_scale(d, min / dist));
}

void camera_init(camera *c, const player *p) {
    c->up = p->up;
    c->fwd = p->facing;
    c->pitch = 0;
    c->view = c->fwd;
    c->view_up = c->up;
    c->pos = v3_add(v3_add(p->pos, v3_scale(c->up, CAM_UP)), v3_scale(c->fwd, -CAM_BACK));
    c->target = p->pos;
}

void camera_update(camera *c, const world *w, const player *p, const camera_input *in, float dt) {
    c->up = v3_norm(v3_lerp(c->up, p->up, ease(4.0f, dt)));

    if (p->grounded) {
        if (in->turn != 0) c->fwd = v3_rotate(c->fwd, c->up, -in->turn * TURN_RATE * dt);
        else if (v3_len(p->move_dir) > 0.05f)
            c->fwd = v3_lerp(c->fwd, p->facing, ease(2.5f, dt));
        c->pitch += (0 - c->pitch) * ease(6.0f, dt);
    } else {
        float yaw = in->look_x * LOOK_YAW + in->turn * TURN_RATE;
        if (yaw != 0) c->fwd = v3_rotate(c->fwd, c->up, -yaw * dt);
        c->pitch += in->look_y * LOOK_PITCH * dt;
        if (c->pitch > PITCH_LIMIT) c->pitch = PITCH_LIMIT;
        if (c->pitch < -PITCH_LIMIT) c->pitch = -PITCH_LIMIT;
    }
    finish(c, w, p);
}
