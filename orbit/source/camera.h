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
    int first_person;   /* requested view */
    float fp;           /* 0 = third person, 1 = inside the head, eased between */
    int gyro;           /* first-person gyro look (Majora's Mask style); only while first_person */
} camera;

void camera_toggle_view(camera *c);   /* leaving first person also switches gyro look off */
void camera_toggle_gyro(camera *c);   /* no effect in third person */

typedef struct {
    float turn;    /* -1..1 roll from L/R in flight; unused on the ground */
    float look_x;  /* -1..1 yaw from the circle pad: free look on the ground and in flight */
    float look_y;  /* -1..1 pitch from the circle pad: limited on the ground, unlimited in flight */
    float gyro_pitch; /* rad/s from the gyroscope, positive tilts the look up; added to the pad */
    float gyro_yaw;   /* rad/s from the gyroscope, positive turns the look left */
} camera_input;

void camera_init(camera *c, const player *p);
void camera_update(camera *c, const world *w, const player *p, const camera_input *in, float dt);

#endif
