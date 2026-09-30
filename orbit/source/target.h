/* Outer Wilds style lock-on: brackets on the body under the crosshair while flying,
   D-pad up locks it, then a ring, name, distance and a relative-velocity indicator. */
#ifndef TARGET_H
#define TARGET_H
#include <citro2d.h>
#include "world.h"
#include "player.h"
#include "camera.h"

typedef struct {
    int candidate;   /* body under the crosshair, -1 if none */
    int locked;      /* body locked on, -1 if none */
} target_state;

void target_init(target_state *t);
/* toggle: D-pad up went down this frame. Only active while flying. */
void target_update(target_state *t, const world *w, const player *p, const camera *c, int toggle);
/* Draws over the finished 3D frame with citro2d (call after C2D_Prepare). */
void target_draw(C3D_RenderTarget *top, const target_state *t, const world *w, const player *p, const camera *c);

#endif
