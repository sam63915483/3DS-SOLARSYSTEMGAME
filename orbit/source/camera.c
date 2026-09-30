#include "camera.h"

#define CAM_BACK     7.0f
#define CAM_UP       2.2f
#define ROLL_RATE    2.0f   /* shoulder buttons in flight, rad/s */
#define LOOK_YAW     2.2f   /* circle pad in flight, rad/s */
#define LOOK_PITCH   2.0f
#define GROUND_PITCH_RATE 1.6f  /* d-pad on the ground, rad/s */
#define GROUND_PITCH_MAX  1.2f  /* about 69 degrees */
#define LEVEL_TIME        0.6f  /* after takeoff the view eases back to level for this long */
#define LEVEL_RATE        9.0f

static void orthonormalize(camera *c) {
    c->view = v3_norm(c->view);
    c->view_up = v3_tangent(c->view_up, c->view);
    c->view_up = v3_len(c->view_up) < 1e-4f ? v3_any_perp(c->view) : v3_norm(c->view_up);
}

#define EYE_HEIGHT 0.72f
#define VIEW_SWITCH_TIME 0.6f

static void place(camera *c, const world *w, const player *p, float dt) {
    /* third person: behind and above, kept out of the ground */
    v3 tp_pos = v3_add(v3_add(p->pos, v3_scale(c->view_up, CAM_UP)), v3_scale(c->view, -CAM_BACK));
    v3 tp_target = v3_add(p->pos, v3_scale(c->view_up, 1.0f));
    const body *b = &w->b[p->dom];
    v3 d = v3_sub(tp_pos, b->pos);
    float dist = v3_len(d);
    if (dist < b->radius + b->amp + 1.0f) {   /* close enough to care: keep it above the terrain */
        float min = world_surface(w, p->dom, v3_norm(d)) + 0.6f;
        if (dist < min) tp_pos = v3_add(b->pos, v3_scale(d, min / dist));
    }

    /* first person: at the eyes, looking along the view */
    v3 fp_pos = v3_add(p->pos, v3_scale(c->view_up, EYE_HEIGHT));
    v3 fp_target = v3_add(fp_pos, c->view);

    /* ease between them */
    float goal = c->first_person ? 1.0f : 0.0f;
    float step = dt / VIEW_SWITCH_TIME;
    if (c->fp < goal) { c->fp += step; if (c->fp > goal) c->fp = goal; }
    if (c->fp > goal) { c->fp -= step; if (c->fp < goal) c->fp = goal; }
    float s = c->fp * c->fp * (3.0f - 2.0f * c->fp);   /* smoothstep */
    c->pos = v3_lerp(tp_pos, fp_pos, s);
    c->target = v3_lerp(tp_target, fp_target, s);
}

void camera_toggle_view(camera *c) {
    c->first_person = !c->first_person;
    if (!c->first_person) c->gyro = 0;
}

void camera_toggle_gyro(camera *c) {
    if (c->first_person) c->gyro = !c->gyro;
}

void camera_init(camera *c, const player *p) {
    c->up = p->up;
    c->fwd = p->facing;
    c->view = c->fwd;
    c->view_up = c->up;
    c->ground_pitch = 0;
    c->first_person = 0;
    c->fp = 0;
    c->gyro = 0;
    c->was_grounded = player_on_foot(p);
    c->level_t = 0;
    c->pos = v3_add(v3_add(p->pos, v3_scale(c->up, CAM_UP)), v3_scale(c->fwd, -CAM_BACK));
    c->target = p->pos;
}

void camera_update(camera *c, const world *w, const player *p, const camera_input *in, float dt) {
    c->up = v3_norm(v3_lerp(c->up, p->up, ease(4.0f, dt)));

    if (player_on_foot(p)) {
        if (!c->was_grounded) {
            /* just landed: keep looking the way we were flying */
            v3 h = v3_tangent(c->view, c->up);
            if (v3_len(h) > 1e-3f) c->fwd = v3_norm(h);
            c->ground_pitch = 0;
        }
        /* circle pad left/right (and the gyro) orbit the look around the standing player */
        float a = (-in->look_x * LOOK_YAW + in->gyro_yaw) * dt;
        if (a != 0) {
            c->fwd = v3_rotate(c->fwd, c->up, a);
            c->view = v3_rotate(c->view, c->up, a);
            c->view_up = v3_rotate(c->view_up, c->up, a);
        }
        c->fwd = v3_tangent(c->fwd, c->up);
        c->fwd = v3_len(c->fwd) < 1e-3f ? v3_any_perp(c->up) : v3_norm(c->fwd);

        /* circle pad up/down (and the gyro) pitch the look, within limits; walking keeps it */
        c->ground_pitch += (in->look_y * GROUND_PITCH_RATE + in->gyro_pitch) * dt;
        if (c->ground_pitch > GROUND_PITCH_MAX) c->ground_pitch = GROUND_PITCH_MAX;
        if (c->ground_pitch < -GROUND_PITCH_MAX) c->ground_pitch = -GROUND_PITCH_MAX;
        float cp = cosf(c->ground_pitch), sp = sinf(c->ground_pitch);
        v3 tv = v3_add(v3_scale(c->fwd, cp), v3_scale(c->up, sp));
        v3 tu = v3_sub(v3_scale(c->up, cp), v3_scale(c->fwd, sp));

        /* settle the free orientation onto the ground basis */
        float k = ease(8.0f, dt);
        c->view = v3_lerp(c->view, tv, k);
        c->view_up = v3_lerp(c->view_up, tu, k);
        orthonormalize(c);
    } else {
        if (c->was_grounded) c->level_t = LEVEL_TIME;   /* just took off */
        if (c->level_t > 0) {
            /* ease the look back to level so the body can stay upright through the jump;
               the pull fades out as the pad takes over */
            v3 tv = v3_tangent(c->view, c->up);
            tv = v3_len(tv) < 1e-3f ? c->fwd : v3_norm(tv);
            float k = ease(LEVEL_RATE * (c->level_t / LEVEL_TIME), dt);
            c->view = v3_lerp(c->view, tv, k);
            c->view_up = v3_lerp(c->view_up, c->up, k);
            orthonormalize(c);
            c->level_t -= dt;
        }
        /* free flight: pitch around right, yaw around view up, roll around view */
        v3 right = v3_norm(v3_cross(c->view, c->view_up));
        float pitch = (in->look_y * LOOK_PITCH + in->gyro_pitch) * dt;
        if (pitch != 0) {
            c->view = v3_rotate(c->view, right, pitch);
            c->view_up = v3_rotate(c->view_up, right, pitch);
        }
        float yaw = (-in->look_x * LOOK_YAW + in->gyro_yaw) * dt;
        if (yaw != 0) c->view = v3_rotate(c->view, c->view_up, yaw);
        float roll = in->turn * ROLL_RATE * dt;
        if (roll != 0) c->view_up = v3_rotate(c->view_up, c->view, roll);
        orthonormalize(c);

        /* heading for the walk controls follows the view so landing feels continuous */
        v3 h = v3_tangent(c->view, c->up);
        if (v3_len(h) > 1e-3f) c->fwd = v3_norm(h);
    }
    c->was_grounded = player_on_foot(p);
    place(c, w, p, dt);
}
