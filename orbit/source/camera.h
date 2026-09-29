/* Third-person chase camera that rolls with local gravity and pitches in flight. */
#ifndef CAMERA_H
#define CAMERA_H
#include "player.h"

typedef struct {
    v3 up, fwd;       /* smoothed gravity up and the heading tangent to it */
    float pitch;      /* radians above the heading, only nonzero in flight */
    v3 view, view_up; /* pitched look direction and the matching up vector */
    v3 pos, target;   /* result for the view matrix */
} camera;

typedef struct {
    float turn;    /* -1..1 yaw from the shoulder buttons */
    float look_x;  /* -1..1 yaw from the circle pad, only used in flight */
    float look_y;  /* -1..1 pitch from the circle pad, up is positive */
} camera_input;

void camera_init(camera *c, const player *p);
void camera_update(camera *c, const world *w, const player *p, const camera_input *in, float dt);

#endif
