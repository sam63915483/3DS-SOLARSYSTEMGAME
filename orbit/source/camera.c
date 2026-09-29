#include "camera.h"

#define CAM_BACK     7.0f
#define CAM_UP       2.2f
#define TURN_RATE    2.0f   /* shoulder buttons on the ground, rad/s */
#define ROLL_RATE    2.0f   /* shoulder buttons in flight, rad/s */
#define LOOK_YAW     2.2f   /* circle pad in flight, rad/s */
#define LOOK_PITCH   2.0f

static void orthonormalize(camera *c) {
    c->view = v3_norm(c->view);
    c->view_up = v3_tangent(c->view_up, c->view);
    c->view_up = v3_len(c->view_up) < 1e-4f ? v3_any_perp(c->view) : v3_norm(c->view_up);
}

static void place(camera *c, const world *w, const player *p) {
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
    c->view = c->fwd;
    c->view_up = c->up;
    c->was_grounded = p->grounded;
    c->pos = v3_add(v3_add(p->pos, v3_scale(c->up, CAM_UP)), v3_scale(c->fwd, -CAM_BACK));
    c->target = p->pos;
}

void camera_update(camera *c, const world *w, const player *p, const camera_input *in, float dt) {
    c->up = v3_norm(v3_lerp(c->up, p->up, ease(4.0f, dt)));

    if (p->grounded) {
        if (!c->was_grounded) {
            /* just landed: keep looking the way we were flying */
            v3 h = v3_tangent(c->view, c->up);
            if (v3_len(h) > 1e-3f) c->fwd = v3_norm(h);
        }
        if (in->turn != 0) {
            float a = -in->turn * TURN_RATE * dt;
            c->fwd = v3_rotate(c->fwd, c->up, a);
            c->view = v3_rotate(c->view, c->up, a);
            c->view_up = v3_rotate(c->view_up, c->up, a);
        } else if (v3_len(p->move_dir) > 0.05f)
            c->fwd = v3_lerp(c->fwd, p->facing, ease(2.5f, dt));
        c->fwd = v3_tangent(c->fwd, c->up);
        c->fwd = v3_len(c->fwd) < 1e-3f ? v3_any_perp(c->up) : v3_norm(c->fwd);

        /* settle the free orientation onto the ground basis */
        float k = ease(6.0f, dt);
        c->view = v3_lerp(c->view, c->fwd, k);
        c->view_up = v3_lerp(c->view_up, c->up, k);
        orthonormalize(c);
    } else {
        /* free flight: pitch around right, yaw around view up, roll around view */
        v3 right = v3_norm(v3_cross(c->view, c->view_up));
        float pitch = in->look_y * LOOK_PITCH * dt;
        if (pitch != 0) {
            c->view = v3_rotate(c->view, right, pitch);
            c->view_up = v3_rotate(c->view_up, right, pitch);
        }
        float yaw = -in->look_x * LOOK_YAW * dt;
        if (yaw != 0) c->view = v3_rotate(c->view, c->view_up, yaw);
        float roll = in->turn * ROLL_RATE * dt;
        if (roll != 0) c->view_up = v3_rotate(c->view_up, c->view, roll);
        orthonormalize(c);

        /* heading for the walk controls follows the view so landing feels continuous */
        v3 h = v3_tangent(c->view, c->up);
        if (v3_len(h) > 1e-3f) c->fwd = v3_norm(h);
    }
    c->was_grounded = p->grounded;
    place(c, w, p);
}
