/* Third-person chase camera that rolls with local gravity. */
#ifndef CAMERA_H
#define CAMERA_H
#include "player.h"

typedef struct {
    v3 up, fwd;     /* smoothed basis */
    v3 pos, target; /* result for the view matrix */
} camera;

void camera_init(camera *c, const player *p);
/* turn: -1..1 from the shoulder buttons */
void camera_update(camera *c, const world *w, const player *p, float turn, float dt);

#endif
