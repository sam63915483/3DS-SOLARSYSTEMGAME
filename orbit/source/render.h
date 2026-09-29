/* citro3d rendering of the top screen. */
#ifndef RENDER_H
#define RENDER_H
#include "world.h"
#include "player.h"
#include "camera.h"

void render_init(const world *w);
/* Draws the whole 3D scene to the top screen. Call between C3D_FrameBegin / C3D_FrameEnd. */
void render_scene(const world *w, const player *p, const camera *c, float time);
void render_fini(void);

#endif
