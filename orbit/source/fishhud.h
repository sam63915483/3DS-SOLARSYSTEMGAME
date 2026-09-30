/* Top-screen fishing overlay drawn with citro2d: tension bar, HOOK! prompt, CAUGHT banner. */
#ifndef FISHHUD_H
#define FISHHUD_H
#include <citro2d.h>
#include "fishing.h"

void fishhud_draw(C3D_RenderTarget *top, const fishing *f, float dt);

#endif
