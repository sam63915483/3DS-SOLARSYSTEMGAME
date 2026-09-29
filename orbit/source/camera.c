#include "camera.h"

#define CAM_BACK   7.0f
#define CAM_UP     2.2f
#define TURN_RATE  2.0f

void camera_init(camera *c, const player *p) {
    c->up = p->up;
    c->fwd = p->facing;
    c->pos = v3_add(v3_add(p->pos, v3_scale(c->up, CAM_UP)), v3_scale(c->fwd, -CAM_BACK));
    c->target = p->pos;
}

void camera_update(camera *c, const world *w, const player *p, float turn, float dt) {
    c->up = v3_norm(v3_lerp(c->up, p->up, ease(4.0f, dt)));

    if (turn != 0) c->fwd = v3_rotate(c->fwd, c->up, -turn * TURN_RATE * dt);
    else if (v3_len(p->move_dir) > 0.05f)
        c->fwd = v3_lerp(c->fwd, p->facing, ease(2.5f, dt));

    c->fwd = v3_tangent(c->fwd, c->up);
    c->fwd = v3_len(c->fwd) < 1e-3f ? v3_any_perp(c->up) : v3_norm(c->fwd);

    c->pos = v3_add(v3_add(p->pos, v3_scale(c->up, CAM_UP)), v3_scale(c->fwd, -CAM_BACK));
    c->target = v3_add(p->pos, v3_scale(c->up, 1.0f));

    /* keep the camera out of the ground */
    const body *b = &w->b[p->dom];
    v3 d = v3_sub(c->pos, b->pos);
    float dist = v3_len(d), min = b->radius + 0.6f;
    if (dist < min) c->pos = v3_add(b->pos, v3_scale(d, min / dist));
}
