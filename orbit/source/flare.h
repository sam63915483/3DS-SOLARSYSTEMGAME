/* Screen-space lens flare drawn with citro2d over the finished 3D frame. */
#ifndef FLARE_H
#define FLARE_H
#include <citro2d.h>

/* sx, sy: the sun's position in top-screen pixels (400 x 240). strength 0..1, 0 draws nothing. */
void flare_draw(C3D_RenderTarget *top, float sx, float sy, float strength);

#endif
