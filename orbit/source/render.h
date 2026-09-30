/* citro3d rendering of the top screen. */
#ifndef RENDER_H
#define RENDER_H
#include "world.h"
#include "player.h"
#include "camera.h"
#include "fishing.h"

void render_init(const world *w);
/* Draws the whole 3D scene to the top screen. Call between C3D_FrameBegin / C3D_FrameEnd. */
void render_scene(const world *w, const player *p, const camera *c, const fishing *f, float time);
/* After render_scene: the sun's top-screen position and flare strength 0..1 (0 if hidden). */
float render_sun_screen(float *sx, float *sy);
/* Projects a world point to top-screen pixels using the last frame's camera; 0 if behind. */
int render_project(v3 p, float *sx, float *sy);
C3D_RenderTarget *render_top_target(void);
void render_fini(void);

#endif
