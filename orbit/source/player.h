/* Third-person player: walks on planets, swims in their seas, flies like a ship once airborne. */
#ifndef PLAYER_H
#define PLAYER_H
#include "world.h"

#define PLAYER_HALF_HEIGHT 0.9f

typedef struct {
    v3 pos;          /* world */
    v3 vel;          /* relative to the dominant body */
    int dom;         /* dominant body index */
    int grounded;
    int swimming;    /* floating at the sea surface: ground controls, no gravity */
    int in_water;    /* any part of the body below the sea surface */
    int thrusting;   /* any jet firing */
    int jet_up, jet_down, jet_fwd;  /* which jets are firing this frame */
    float jet_roll;  /* -1..1 roll jets (visual only) */
    v3 brake_dir;    /* world-space direction the brake jet fires (along travel), zero if not braking */
    v3 up;           /* away from the dominant body */
    v3 facing;       /* unit tangent, where the model looks on the ground */
    v3 move_dir;     /* world-space input direction from the last update (0 if none) */
    float speed;     /* |vel| for the HUD */
    float altitude;  /* height above the dominant body's surface */
    float anim;      /* walk cycle phase, advances with ground speed */
} player;

typedef struct {
    float mx, my;      /* walk input while grounded or swimming, -1..1, my > 0 is forward */
    int jump_pressed;  /* X went down this frame */
    int thrust_fwd;    /* A held in the air: thrust along view_forward */
    int thrust_up;     /* X held in the air: thrust along view_up */
    int thrust_down;   /* B held in the air: thrust against view_up */
    int brake;         /* Y held in the air: bleed relative velocity */
    float roll;        /* -1..1 from L/R in the air, only drives the roll jets */
    v3 cam_forward;    /* camera heading, any length; walking is relative to it */
    v3 view_forward;   /* unit vector the camera looks along, including pitch */
    v3 view_up;        /* unit vector up from the player as seen by the camera; up/down thrust follow it */
} player_input;

void player_spawn(player *p, const world *w, int body);
void player_update(player *p, const world *w, const player_input *in, float dt);
/* True when the player should use ground-style controls and camera. */
static inline int player_on_foot(const player *p) { return p->grounded || p->swimming; }

#endif
