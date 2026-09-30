/* Navball instrument for the top screen: orientation relative to the dominant body plus jet lights. */
#ifndef GIMBAL_H
#define GIMBAL_H
#include <citro2d.h>
#include "world.h"
#include "player.h"
#include "camera.h"

void gimbal_draw(C3D_RenderTarget *top, const world *w, const player *p, const camera *c);

#endif
