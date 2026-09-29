/* Bottom-screen text HUD drawn with citro2d. */
#ifndef HUD_H
#define HUD_H
#include <citro2d.h>
#include "world.h"
#include "player.h"

void hud_init(void);
void hud_draw(C3D_RenderTarget *bottom, const world *w, const player *p, float fps);
void hud_fini(void);

#endif
