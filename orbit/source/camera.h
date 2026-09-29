/* Third-person chase camera: gravity-aligned on the ground, free orientation in flight. */
#ifndef CAMERA_H
#define CAMERA_H
#include "player.h"

typedef struct {
    v3 up, fwd;       /* smoothed gravity up and the walking heading tangent to it */
    v3 view, view_up; /* the orientation actually used: free in flight, eases to fwd/up on the ground */
    v3 pos, target;   /* result for the view matrix */
    float ground_pitch; /* d-pad look up/down on the ground, radians */
    int was_grounded;
} camera;

typedef struct {
    float turn;    /* -1..1: yaw from the d-pad on the ground, roll from L/R in flight */
    float look_x;  /* -1..1 yaw from the circle pad, only used in flight */
    float look_y;  /* -1..1 pitch: d-pad on the ground (limited), circle pad in flight (unlimited) */
} camera_input;

void camera_init(camera *c, const player *p);
void camera_update(camera *c, const world *w, const player *p, const camera_input *in, float dt);

#endif
