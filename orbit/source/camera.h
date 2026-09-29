/* Third-person chase camera: gravity-aligned on the ground, free orientation in flight. */
#ifndef CAMERA_H
#define CAMERA_H
#include "player.h"

typedef struct {
    v3 up, fwd;       /* smoothed gravity up and the walking heading tangent to it */
    v3 view, view_up; /* the orientation actually used: free in flight, eases to fwd/up on the ground */
    v3 pos, target;   /* result for the view matrix */
    int was_grounded;
} camera;

typedef struct {
    float turn;    /* -1..1: yaw from the shoulder buttons on the ground, roll in flight */
    float look_x;  /* -1..1 yaw from the circle pad, only used in flight */
    float look_y;  /* -1..1 pitch from the circle pad, up is positive, unlimited */
} camera_input;

void camera_init(camera *c, const player *p);
void camera_update(camera *c, const world *w, const player *p, const camera_input *in, float dt);

#endif
