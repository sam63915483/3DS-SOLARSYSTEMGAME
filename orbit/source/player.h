/* Third-person player with gravity-relative jetpack controls. */
#ifndef PLAYER_H
#define PLAYER_H
#include "world.h"

#define PLAYER_HALF_HEIGHT 0.9f

typedef struct {
    v3 pos;          /* world */
    v3 vel;          /* relative to the dominant body */
    int dom;         /* dominant body index */
    int grounded;
    int thrusting;   /* jetpack flame on */
    v3 up;           /* away from the dominant body */
    v3 facing;       /* unit tangent, where the model looks */
    v3 move_dir;     /* world-space input direction from the last update (0 if none) */
    float speed;     /* |vel| for the HUD */
    float altitude;  /* height above the dominant body's surface */
} player;

typedef struct {
    float mx, my;      /* circle pad, -1..1, my > 0 is forward */
    int jump_pressed;  /* B went down this frame */
    int thrust_up;     /* B held */
    int thrust_down;   /* A held */
    int brake;         /* Y held */
    v3 cam_forward;    /* world-space camera forward, any length */
} player_input;

void player_spawn(player *p, const world *w, int body);
void player_update(player *p, const world *w, const player_input *in, float dt);

#endif
