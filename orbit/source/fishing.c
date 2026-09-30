#include "fishing.h"
#include <string.h>
#include <stdio.h>

#define ROD_LEN        1.6f
#define ROD_REST_ELEV 35.0f    /* degrees above the hand's forward */
#define EQUIP_TIME     0.4f
#define HANG_LEASH     0.75f
#define LEASH_GROW     1.4f
#define CHARGE_TIME    2.0f
#define DRAW_TIME      0.15f
#define DRAW_ANGLE    50.0f    /* degrees the rod whips back on any press */
#define CHARGE_ANGLE  60.0f    /* degrees more it eases back over a full charge */
#define CAST_SPEED_MIN  6.5f   /* tap: about 5 units on Verdant */
#define CAST_SPEED_MAX 20.0f   /* full hold: about 15 */
#define REEL_SPEED     6.6f
#define REEL_STEP_MAX 17.0f
#define TAUT_ON       (1.0f / 0.45f)
#define TAUT_OFF      (1.0f / 1.2f)
#define ARRIVE_DIST    1.6f
#define HANG_DIST      1.1f
#define APPROACH_TIME  5.38f
#define RETREAT_TIME   2.4f
#define BANNER_TIME    2.5f
#define BOBBER_R       0.12f
#define DEG ((float)M_PI / 180.0f)

static const float STRIKE_WINDOW[3] = { 3.0f, 2.0f, 1.5f };
static const float FISH_SCALE[3] = { 1.6f, 2.4f, 3.4f };   /* of the 0.5-long mesh: 0.8 / 1.2 / 1.7 long */
#define ARM_REST_SWING (-1.3f)   /* radians the right arm is swung about the shoulder at rest: hand out in front */
static const float WEIGHT_LO[3] = { 1.0f, 3.0f, 9.0f }, WEIGHT_HI[3] = { 3.0f, 9.0f, 26.0f };   /* pounds */
static const char *TIER_NAME[3] = { "COMMON", "UNCOMMON", "RARE" };

const char *fishing_tier_name(int tier) { return TIER_NAME[tier < 0 ? 0 : (tier > 2 ? 2 : tier)]; }

static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float smoothstep(float x) { x = clamp01(x); return x * x * (3 - 2 * x); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float toward(float x, float target, float step) {
    if (x < target) { x += step; return x > target ? target : x; }
    x -= step; return x < target ? target : x;
}
static v3 v3_toward(v3 a, v3 b, float step) {
    v3 d = v3_sub(b, a); float l = v3_len(d);
    return l <= step ? b : v3_add(a, v3_scale(d, step / l));
}

/* ---- rod ---- */

void fishing_rod_frame(const fishing *f, rod_frame *r) {
    /* pose and recoil swing the rod forward (Unity's positive X), haul pulls it back */
    float e = (ROD_REST_ELEV - f->rod_pose - f->rod_recoil + f->rod_haul) * DEG;
    r->butt = f->hand;
    r->dir = v3_norm(v3_add(v3_scale(f->hand_fwd, cosf(e)), v3_scale(f->hand_up, sinf(e))));
    r->bend_rad = f->rod_bend * DEG;
    v3 axis = v3_cross(r->dir, v3_sub(f->bobber, f->hand));   /* bends toward the bobber */
    r->bend_axis = v3_len(axis) > 1e-4f ? v3_norm(axis) : v3_any_perp(r->dir);
}

v3 fishing_rod_point(const rod_frame *r, float s) {
    return v3_add(r->butt, v3_rotate(v3_scale(r->dir, ROD_LEN * s), r->bend_axis, r->bend_rad * s * s));
}

float fishing_arm_swing(const fishing *f) {
    if (!fishing_active(f)) return ARM_REST_SWING;
    /* the arm turns with the rod so the two stay rigid: as the rod tilts back the hand rises
       over the head and behind it (swing more negative), then whips forward for the cast */
    float e = ROD_REST_ELEV - f->rod_pose - f->rod_recoil + f->rod_haul;
    float a = ARM_REST_SWING - (e - ROD_REST_ELEV) * DEG;
    if (a > 0.3f) a = 0.3f;
    if (a < -3.6f) a = -3.6f;
    return a;
}

static v3 tip_of(const fishing *f) { rod_frame r; fishing_rod_frame(f, &r); return fishing_rod_point(&r, 1.0f); }

static void tween(fishing *f, float to, float len) { f->tw_from = f->rod_pose; f->tw_to = to; f->tw_t = 0; f->tw_len = len; }

/* ---- geometry around the dominant body ---- */

static v3 up_at(const world *w, const player *p, v3 pos) { return v3_norm(v3_sub(pos, w->b[p->dom].pos)); }
/* distance along the ground between the player and pos, in the player's tangent plane */
static float surf_dist(const player *p, v3 pos) { return v3_len(v3_tangent(v3_sub(pos, p->pos), p->up)); }
static v3 toward_player(const player *p, v3 pos) {
    v3 t = v3_tangent(v3_sub(p->pos, pos), p->up);
    return v3_len(t) > 1e-4f ? v3_norm(t) : v3_any_perp(p->up);
}
static v3 on_sea(const world *w, const player *p, v3 pos) {
    return v3_add(w->b[p->dom].pos, v3_scale(up_at(w, p, pos), world_sea_radius(w, p->dom) - 0.01f));
}

/* ---- state helpers ---- */

static void enter(fishing *f, fishing_state s) { f->state = s; f->t = 0; }

static void clear_tackle(fishing *f) {
    f->bobber_visible = f->line_visible = f->fish_visible = 0;
    f->reeling = 0; f->retrieve_taut = 0; f->line_taut = 0;
    f->charge = 0; f->draw_t = 0;
    f->hookset_pending = 0; f->with_fish = 0;
    f->fight.outcome = FIGHT_LANDED;
    f->rod_bend = f->rod_haul = 0;
}

static void new_bite_timer(fishing *f) { f->bite_timer = frandf(&f->rng, 5.0f, 14.0f); }

static int roll_tier(fishing *f, float d) {
    float k = clamp01((d - 4.0f) / 10.0f);   /* long casts skew a little rarer: 70/25/5 up to 58/32/10 */
    float wc = 70 - 12 * k, wu = 25 + 7 * k, wr = 5 + 5 * k;
    float x = frandf(&f->rng, 0, wc + wu + wr);
    return x < wc ? 0 : (x < wc + wu ? 1 : 2);
}

static void start_waiting(fishing *f) {
    enter(f, FISH_WAITING);
    f->fish_visible = 0;
    new_bite_timer(f);
}

static void start_retreat(fishing *f, const player *p) {
    f->retreat_from = f->fish_pos;
    f->retreat_dir = v3_scale(toward_player(p, f->bobber), -1.0f);
    f->wander = f->wander_target = v3_(0, 0, 0);
    enter(f, FISH_RETREAT);
}

static void lost(fishing *f, const char *msg) {
    snprintf(f->lost_msg, sizeof f->lost_msg, "%s", msg);
    f->lost_t = 2.0f;
}

/* The bobber dangling under the tip on a leash: a stiff damped spring toward a point one leash
   below the tip, and a hard rope so it can never be further than the leash. Runs while hanging,
   charging and casting, so the cast flicks it off the swinging rod. */
static void hang_step(fishing *f, v3 tip, float dt) {
    v3 target = v3_sub(tip, v3_scale(f->bobber_up, f->leash));
    v3 acc = v3_sub(v3_scale(v3_sub(target, f->bobber), 45.0f), v3_scale(f->bobber_vel, 7.0f));
    f->bobber_vel = v3_add(f->bobber_vel, v3_scale(acc, dt));
    float sp = v3_len(f->bobber_vel);
    if (sp > 20) f->bobber_vel = v3_scale(f->bobber_vel, 20.0f / sp);
    f->bobber = v3_add(f->bobber, v3_scale(f->bobber_vel, dt));
    v3 d = v3_sub(f->bobber, tip); float l = v3_len(d);
    if (l > f->leash && l > 1e-5f) {
        v3 n = v3_scale(d, 1.0f / l);
        f->bobber = v3_add(tip, v3_scale(n, f->leash));
        float out = v3_dot(f->bobber_vel, n);
        if (out > 0) f->bobber_vel = v3_sub(f->bobber_vel, v3_scale(n, out));
    }
}

static void start_towing(fishing *f, int with_fish) {
    f->with_fish = with_fish;
    f->reeling = 0;
    f->retrieve_taut = 1;
    enter(f, FISH_TOWING);
}

/* ---- fish ---- */

/* Fish hanging off the float: head at the bobber, body pointing away from the angler, yawing. */
static void fish_on_float(fishing *f, const player *p, float vig, int running, float dt) {
    float amp = lerpf(4, 30, vig) * (running ? 1.5f : 1.0f) * DEG;
    float rate = lerpf(2, 10, vig) * (running ? 2.4f : 1.0f);
    f->fish_phase += rate * dt;
    v3 up = f->bobber_up, away = v3_scale(toward_player(p, f->bobber), -1.0f);
    v3 fwd = v3_scale(v3_rotate(away, up, sinf(f->fish_phase) * amp), -1.0f);   /* head towards the float */
    float pitch = (12.0f + (running ? 10.0f : 0.0f)) * DEG;
    fwd = v3_norm(v3_add(v3_scale(fwd, cosf(pitch)), v3_scale(up, sinf(pitch))));
    float half = 0.25f * f->fish_scale;
    f->fish_fwd = fwd;
    f->fish_up = v3_norm(v3_tangent(up, fwd));
    f->fish_pos = v3_add(v3_sub(f->bobber, v3_scale(fwd, half)), v3_scale(up, 0.05f + 0.3f * half));   /* head up out of the water */
    f->fish_visible = 1;
}

static void face_along(fishing *f, v3 pos, v3 up) {
    v3 d = v3_sub(pos, f->fish_pos);
    if (v3_len(d) > 1e-4f) {
        d = v3_norm(d);
        float u = v3_dot(d, up);
        if (fabsf(u) > 0.7071f) {   /* pitch clamped to 45 degrees */
            v3 t = v3_tangent(d, up);
            t = v3_len(t) > 1e-4f ? v3_norm(t) : v3_any_perp(up);
            d = v3_norm(v3_add(v3_scale(t, 0.7071f), v3_scale(up, u > 0 ? 0.7071f : -0.7071f)));
        }
        f->fish_fwd = d;
    }
    f->fish_pos = pos;
    f->fish_up = v3_norm(v3_tangent(up, f->fish_fwd));
    f->fish_visible = 1;
}

/* Helix up from the depths on the far side of the float, level off, pounce. */
static void fish_approach(fishing *f, const player *p, float t) {
    v3 up = f->bobber_up, away = v3_scale(toward_player(p, f->anchor), -1.0f), side = v3_cross(up, away);
    float r, d, ang;
    if (t < 4.5f) {
        float k = t / 4.5f;
        ang = 2.0f * (float)M_PI * 1.4f * k;
        r = lerpf(2.8f, 0.65f, k);
        d = lerpf(f->spawn_depth, 0.5f, smoothstep((k - 0.25f) / 0.75f));
    } else if (t < 5.1f) {
        float k = (t - 4.5f) / 0.6f;
        ang = 2.0f * (float)M_PI * 1.4f;
        r = lerpf(0.65f, 0.5f, k); d = lerpf(0.5f, 0.42f, k);
    } else {
        float k = clamp01((t - 5.1f) / 0.28f); k *= k;
        ang = 2.0f * (float)M_PI * 1.4f;
        r = lerpf(0.5f, 0.02f, k); d = lerpf(0.42f, -0.04f, k);
    }
    v3 pos = v3_add(v3_add(f->anchor, v3_scale(away, r * cosf(ang))), v3_add(v3_scale(side, r * sinf(ang)), v3_scale(up, -d)));
    face_along(f, pos, up);
}

static void fish_retreat(fishing *f, float t) {
    float k = clamp01(t / RETREAT_TIME);
    v3 pos = v3_add(v3_add(f->retreat_from, v3_scale(f->retreat_dir, 5.5f * k)), v3_scale(f->bobber_up, -2.5f * k * k));
    face_along(f, pos, f->bobber_up);
    if (t >= RETREAT_TIME) f->fish_visible = 0;
}

/* ---- bobber on the water ---- */

static void update_wander(fishing *f, const player *p, float vig, float dt) {
    f->wander_t -= dt;
    if (f->wander_t <= 0) {
        float v = vig < 0.05f ? 0.05f : vig;
        f->wander_t = frandf(&f->rng, 0.6f, 1.8f) / v;
        v3 up = f->bobber_up, away = v3_scale(toward_player(p, f->anchor), -1.0f), side = v3_cross(up, away);
        f->wander_target = v3_add(v3_scale(side, frandf(&f->rng, -1.0f, 1.0f)), v3_scale(away, frandf(&f->rng, -0.7f, 0.7f)));
    }
    f->wander = v3_toward(f->wander, f->wander_target, 0.5f * dt);
}

/* Slides the anchor along the sea toward the player by up to step. */
static void reel_anchor(fishing *f, const world *w, const player *p, float step) {
    float d = surf_dist(p, f->anchor);
    if (step > d) step = d;
    f->anchor = on_sea(w, p, v3_add(f->anchor, v3_scale(toward_player(p, f->anchor), step)));
}

/* True when the reeled bobber has reached the angler or run up onto the shore. */
static int reel_arrived(const fishing *f, const world *w, const player *p) {
    if (surf_dist(p, f->anchor) <= ARRIVE_DIST) return 1;
    v3 ahead = v3_add(f->anchor, v3_scale(toward_player(p, f->anchor), 0.35f));
    v3 d = v3_sub(ahead, w->b[p->dom].pos);
    return world_surface(w, p->dom, v3_norm(d)) > world_sea_radius(w, p->dom);
}

static void update_retrieve_taut(fishing *f, const fishing_input *in, float dt) {
    f->reeling = in->r_down && f->r_released_since_cast;
    f->retrieve_taut = toward(f->retrieve_taut, f->reeling ? 1.0f : 0.0f, (f->reeling ? TAUT_ON : TAUT_OFF) * dt);
}

/* ---- main update ---- */

void fishing_init(fishing *f) {
    memset(f, 0, sizeof *f);
    f->state = FISH_OFF;
    f->rng = 0x9E3779B9u;
    f->recoil_t = -1;
    f->lost_bite_bonus = 1;
    f->fight.outcome = FIGHT_LANDED;
    f->splash_t = 10;
}

void fishing_update(fishing *f, const world *w, const player *p, const fishing_input *in, float dt) {
    const body *dom = &w->b[p->dom];
    f->hand = in->hand; f->hand_fwd = in->hand_fwd; f->hand_up = in->hand_up;
    f->time += dt;
    if (f->banner_t > 0) f->banner_t -= dt;
    if (f->lost_t > 0) f->lost_t -= dt;
    f->bar_shake -= 3.5f * dt; if (f->bar_shake < 0) f->bar_shake = 0;
    f->splash_t += dt;

    /* the rod stays out on the ground and while hopping about inside the atmosphere; it goes
       away when you fall in the sea or fly out into free look, where R rolls instead */
    int on_ground = player_locked(p, w) && !p->swimming;
    if (f->state != FISH_OFF && !on_ground) {
        clear_tackle(f);
        f->state = FISH_OFF;
        return;
    }
    if (f->state == FISH_OFF) {
        if (!(in->equip_pressed && on_ground)) return;
        clear_tackle(f);
        f->rod_pose = -120; tween(f, 0, EQUIP_TIME);
        f->bobber = tip_of(f); f->bobber_vel = v3_(0, 0, 0);
        f->bobber_visible = 1;
        enter(f, FISH_EQUIPPING);
    } else if (in->equip_pressed && f->state != FISH_UNEQUIPPING) {
        clear_tackle(f);
        tween(f, 180, EQUIP_TIME);
        enter(f, FISH_UNEQUIPPING);
    }

    /* everything rides along with the dominant body */
    v3 ride = v3_sub(dom->pos, dom->prev);
    f->bobber = v3_add(f->bobber, ride);
    f->anchor = v3_add(f->anchor, ride);
    f->fish_pos = v3_add(f->fish_pos, ride);
    f->retreat_from = v3_add(f->retreat_from, ride);
    f->splash_pos = v3_add(f->splash_pos, ride);
    f->bobber_up = up_at(w, p, f->bobber);
    if (!in->r_down) f->r_released_since_cast = 1;

    f->t += dt;
    if (f->tw_len > 0) {
        f->tw_t += dt;
        f->rod_pose = lerpf(f->tw_from, f->tw_to, smoothstep(f->tw_t / f->tw_len));
        if (f->tw_t >= f->tw_len) f->tw_len = 0;
    }
    if (f->recoil_t >= 0) {   /* snap: whip forward, then settle */
        f->recoil_t += dt;
        f->rod_recoil = f->recoil_t < 0.06f ? 42.0f * f->recoil_t / 0.06f : 42.0f * (1.0f - clamp01((f->recoil_t - 0.06f) / 0.45f));
        if (f->recoil_t > 0.51f) { f->rod_recoil = 0; f->recoil_t = -1; }
    }

    v3 tip = tip_of(f);
    float taut_for_line = f->retrieve_taut;
    int running = 0; float vig = 1;

    switch (f->state) {
    case FISH_OFF: break;

    case FISH_EQUIPPING:
        f->bobber = tip;
        if (f->t >= EQUIP_TIME) { f->leash = 0.02f; f->bobber_vel = v3_(0, 0, 0); enter(f, FISH_HANGING); }
        break;

    case FISH_HANGING:
        /* the leash pays out to its resting length while the bobber dangles */
        f->leash = f->leash > HANG_LEASH ? toward(f->leash, HANG_LEASH, REEL_SPEED * dt) : toward(f->leash, HANG_LEASH, LEASH_GROW * dt);
        hang_step(f, tip, dt);
        f->line_visible = 1;
        taut_for_line = 0.6f;
        if (in->r_pressed) {
            f->charge = 0; f->draw_t = 0; f->tw_len = 0;
            enter(f, FISH_CHARGING);
        }
        break;

    case FISH_CHARGING:
        /* the rod whips back, then eases further back the longer R is held; the bobber keeps
           swinging under the tip the whole time */
        f->charge = clamp01(f->t / CHARGE_TIME);
        f->draw_t = clamp01(f->draw_t + dt / DRAW_TIME);
        f->rod_pose = -(DRAW_ANGLE * f->draw_t + CHARGE_ANGLE * f->charge);
        hang_step(f, tip_of(f), dt);
        f->line_visible = 1;
        taut_for_line = 0.6f;
        if (!in->r_down) {
            f->cast_draw = DRAW_TIME * (1.0f - f->draw_t);
            f->cast_from = f->rod_pose;
            f->cast_back = -(DRAW_ANGLE + CHARGE_ANGLE * f->charge);
            enter(f, FISH_CASTING);
        }
        break;

    case FISH_CASTING: {
        /* finish the draw, hold, snap forward and flick the bobber off the leash at 70 percent
           of the snap, settle */
        float t0 = f->cast_draw, t1 = t0 + 0.02f, t2 = t1 + 0.1f;
        f->line_visible = 1;
        taut_for_line = 0.6f;
        if (f->t < t0) f->rod_pose = lerpf(f->cast_from, f->cast_back, f->t / t0);
        else if (f->t < t1) f->rod_pose = f->cast_back;
        else {
            float k = clamp01((f->t - t1) / (t2 - t1));
            f->rod_pose = lerpf(f->cast_back, 5.0f, smoothstep(k));
            if (k >= 0.7f) {
                float speed = sqrtf(lerpf(CAST_SPEED_MIN * CAST_SPEED_MIN, CAST_SPEED_MAX * CAST_SPEED_MAX, f->charge));
                /* launched from wherever the leash has swung it, along the look, with a little
                   of the swing kept for flavour */
                f->bobber_vel = v3_add(v3_add(p->vel, v3_scale(v3_norm(in->cast_dir), speed)), v3_scale(f->bobber_vel, 0.2f));
                f->r_released_since_cast = 0;
                f->retrieve_taut = 0;
                f->lost_bite_bonus = 1;
                tween(f, 0, 0.13f);
                enter(f, FISH_FLIGHT);
                break;
            }
        }
        hang_step(f, tip_of(f), dt);
        break;
    }

    case FISH_FLIGHT: {
        v3 g = v3_sub(world_gravity_at(w, f->bobber), world_frame_accel(w, p->dom));
        f->bobber_vel = v3_add(f->bobber_vel, v3_scale(g, dt));
        f->bobber = v3_add(f->bobber, v3_scale(f->bobber_vel, dt));
        f->line_visible = 1;
        taut_for_line = 0;
        v3 d = v3_sub(f->bobber, dom->pos);
        float dist = v3_len(d);
        v3 dir = v3_scale(d, 1.0f / dist);
        float sea_r = world_sea_radius(w, p->dom);
        if (sea_r > 0 && dist < sea_r) {
            f->splash_pos = on_sea(w, p, f->bobber); f->splash_t = 0;
            f->anchor = f->bobber = f->splash_pos;
            f->bob_off = 0;
            f->wander = f->wander_target = v3_(0, 0, 0);
            f->bobber_vel = v3_(0, 0, 0);
            start_waiting(f);
        } else if (dist < world_surface(w, p->dom, dir) + BOBBER_R) {
            f->bobber = v3_add(dom->pos, v3_scale(dir, world_surface(w, p->dom, dir) + BOBBER_R));
            f->bobber_vel = v3_(0, 0, 0);
            enter(f, FISH_GROUNDED);
        }
        break;
    }

    case FISH_GROUNDED:
        f->line_visible = 1;
        update_retrieve_taut(f, in, dt);
        if (f->retrieve_taut >= 0.985f) start_towing(f, 0);
        break;

    case FISH_WAITING:
    case FISH_APPROACH:
    case FISH_STRIKE:
    case FISH_FIGHT:
    case FISH_RETREAT: {
        f->line_visible = 1;
        float target_off = (sinf(f->time * 1.2f) - 1.0f) * 0.015f;   /* idle: a dip of up to 3 cm */
        float extra_v = 0, lat = 0, along = 0;
        int fast_follow = 0;

        if (f->state == FISH_WAITING || f->state == FISH_APPROACH || f->state == FISH_RETREAT) {
            update_retrieve_taut(f, in, dt);
            if (f->reeling && f->retrieve_taut >= 0.985f) {
                float step = REEL_SPEED * dt;
                if (step > REEL_STEP_MAX * dt) step = REEL_STEP_MAX * dt;
                reel_anchor(f, w, p, step);
                if (reel_arrived(f, w, p)) { f->fish_visible = 0; start_towing(f, 0); break; }
            }
            if (f->state == FISH_WAITING) {
                f->bite_timer -= dt * (f->reeling ? 1.2f : 1.0f) * f->lost_bite_bonus;
                if (f->bite_timer <= 0) {
                    float d = surf_dist(p, f->anchor);
                    f->tier = roll_tier(f, d);
                    f->weight_frac = frandf(&f->rng, 0, 1);
                    f->weight_frac *= f->weight_frac;   /* mostly small fish, the odd whopper */
                    f->fish_scale = FISH_SCALE[f->tier];
                    float bed = world_sea_radius(w, p->dom) - world_surface(w, p->dom, up_at(w, p, f->anchor));
                    f->spawn_depth = bed - 0.4f;
                    if (f->spawn_depth < 1.2f) f->spawn_depth = 1.2f;
                    if (f->spawn_depth > 7.0f) f->spawn_depth = 7.0f;
                    /* start the fish at its spawn point so its first heading is sane */
                    v3 away = v3_scale(toward_player(p, f->anchor), -1.0f);
                    f->fish_pos = v3_add(v3_add(f->anchor, v3_scale(away, 2.8f)), v3_scale(f->bobber_up, -f->spawn_depth));
                    f->fish_fwd = v3_scale(away, -1.0f);
                    f->fish_up = v3_norm(v3_tangent(f->bobber_up, f->fish_fwd));
                    f->fish_visible = 1;
                    enter(f, FISH_APPROACH);
                }
            } else if (f->state == FISH_APPROACH) {
                fish_approach(f, p, f->t);
                if (f->t >= APPROACH_TIME) {
                    f->strike_window = STRIKE_WINDOW[f->tier];
                    f->wander = f->wander_target = v3_(0, 0, 0); f->wander_t = 0;
                    f->fish_phase = 0;
                    enter(f, FISH_STRIKE);
                }
            } else {
                fish_retreat(f, f->t);
                if (f->t >= RETREAT_TIME) start_waiting(f);
            }
        } else if (f->state == FISH_STRIKE) {
            fast_follow = 1;
            target_off = -0.12f;
            extra_v = (sinf(f->time * 16.0f) * 0.5f - 0.35f) * 0.02f;   /* quick shiver, biased down */
            update_wander(f, p, 1.0f, dt);
            fish_on_float(f, p, 1.0f, 0, dt);
            if (in->r_pressed || (in->r_down && f->reeling)) {
                float d = surf_dist(p, f->anchor);
                fight_start(&f->fight, f->tier, f->weight_frac, d > 2.2f ? d : 2.2f, f->retrieve_taut, f->rng);
                f->rng ^= f->fight.rng * 2654435761u;
                f->hookset_pending = 1;
                f->reeling = 0; f->retrieve_taut = 0;
                f->tug_t = 0; f->tug_phase = 0;
                enter(f, FISH_FIGHT);
            } else if (f->t >= f->strike_window) {
                f->lost_bite_bonus = 1.3f;
                start_retreat(f, p);
            }
        } else {   /* FISH_FIGHT */
            fast_follow = 1;
            fight *g = &f->fight;
            g->distance = surf_dist(p, f->anchor);
            fight_step(g, dt, in->r_down);
            if (g->run_started) f->bar_shake = 1;
            running = g->running; vig = fight_vigour(g);
            {
                float delta = surf_dist(p, f->anchor) - g->distance;   /* positive: reel it in */
                float cap = REEL_STEP_MAX * dt;
                if (delta > cap) delta = cap;
                if (delta < -cap) delta = -cap;
                if (delta > 0) reel_anchor(f, w, p, delta);
                else if (delta < 0) f->anchor = on_sea(w, p, v3_sub(f->anchor, v3_scale(toward_player(p, f->anchor), -delta)));
            }
            taut_for_line = g->taut;
            target_off = -0.28f;
            /* tugs and jitter */
            f->tug_t -= dt;
            if (f->tug_t <= 0) { f->tug_phase = 1; f->tug_t = frandf(&f->rng, 0.35f, 1.1f) / (vig < 0.05f ? 0.05f : vig); }
            f->tug_phase = toward(f->tug_phase, 0, 5.0f * dt);
            float k = f->tug_phase * f->tug_phase;
            extra_v = -0.09f * k * lerpf(0.35f, 1.0f, vig);
            float amp = 0.035f * lerpf(0.2f, 1.0f, vig) * (running ? 1.8f : 1.0f);
            float tt = f->time * 2.0f * (float)M_PI;
            lat = sinf(12.0f * f->tug_phase) * 0.036f * k + amp * 0.5f * (sinf(tt * 13.7f) + sinf(tt * 27.1f));
            along = amp * 0.5f * (sinf(tt * 9.3f) + sinf(tt * 19.7f));
            update_wander(f, p, vig, dt);
            fish_on_float(f, p, vig, running, dt);
            if (f->hookset_pending && (g->taut >= 0.65f || f->t >= 1.5f)) { f->rod_haul = 25; f->hookset_pending = 0; }
            if (g->outcome == FIGHT_LANDED) { start_towing(f, 1); break; }
            if (g->outcome == FIGHT_SNAPPED) { f->recoil_t = 0; f->hookset_pending = 0; lost(f, "FISH BROKE OFF!"); start_retreat(f, p); break; }
            if (g->outcome == FIGHT_SLIPPED) { f->lost_bite_bonus = 1.3f; f->hookset_pending = 0; lost(f, "FISH GOT AWAY!"); start_retreat(f, p); break; }
        }

        /* place the bobber: seated on the anchor, dipped or submerged, plus wander and jitter */
        if (fast_follow) f->bob_off += (target_off - f->bob_off) * (1.0f - expf(-22.0f * dt));
        else f->bob_off = toward(f->bob_off, target_off, 0.6f * dt);
        if (f->state != FISH_STRIKE && f->state != FISH_FIGHT) f->wander = v3_toward(f->wander, v3_(0, 0, 0), 0.5f * dt);
        v3 away = v3_scale(toward_player(p, f->anchor), -1.0f), side = v3_cross(f->bobber_up, away);
        f->bobber = v3_add(v3_add(f->anchor, v3_scale(f->bobber_up, f->bob_off + extra_v)),
                           v3_add(f->wander, v3_add(v3_scale(side, lat), v3_scale(away, along))));
        break;
    }

    case FISH_TOWING: {
        f->line_visible = 1;
        taut_for_line = 1;
        float d = v3_len(v3_sub(tip, f->bobber));
        f->bobber = v3_toward(f->bobber, tip, REEL_SPEED * dt);
        if (f->with_fish) fish_on_float(f, p, 0.3f, 0, dt);
        if (d < HANG_DIST) {
            f->leash = d;
            f->bobber_vel = v3_(0, 0, 0);
            if (f->with_fish) {
                f->caught++;
                float kg = lerpf(WEIGHT_LO[f->tier], WEIGHT_HI[f->tier], f->weight_frac);
                snprintf(f->banner, sizeof f->banner, "CAUGHT! %s %.1f LB", TIER_NAME[f->tier], kg);
                f->banner_t = BANNER_TIME;
                f->fish_visible = 0;
                f->with_fish = 0;
            }
            f->retrieve_taut = 0;
            enter(f, FISH_HANGING);
        }
        break;
    }

    case FISH_UNEQUIPPING:
        if (f->t >= EQUIP_TIME) { f->state = FISH_OFF; f->tw_len = 0; }
        break;
    }

    /* rod bend and haul follow the line: nothing happens until it is tight */
    {
        float load = 0, haul_target = 0;
        if (f->state == FISH_FIGHT) {
            const fight *g = &f->fight;
            load = g->taut * (0.18f + 0.82f * g->tension / 100.0f);
            if (g->stamina <= 0) load *= 0.5f;
            if (in->r_down) haul_target = 18.0f + (g->running ? 7.0f : 0.0f);
        } else if (f->reeling) {
            load = 0.16f * f->retrieve_taut;
            haul_target = 7.2f;
        }
        haul_target *= clamp01((taut_for_line - 0.65f) / 0.35f);
        float curve = load < 0.45f ? load / 0.45f * 0.5f : 0.5f + 0.5f * powf((load - 0.45f) / 0.55f, 1.5f);
        float bend_target = 38.0f * curve;
        f->rod_bend += (bend_target - f->rod_bend) * ease(bend_target > f->rod_bend ? 8.0f : 16.0f, dt);
        f->rod_haul += (haul_target - f->rod_haul) * ease(haul_target > f->rod_haul ? 4.0f : 8.0f, dt);
        f->line_taut = taut_for_line;
    }
}
