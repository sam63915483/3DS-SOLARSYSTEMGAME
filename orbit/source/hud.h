/* Bottom-screen HUD drawn with citro2d and the built-in pixel font. */
#ifndef HUD_H
#define HUD_H
#include <citro2d.h>
#include "world.h"
#include "player.h"
#include "camera.h"

void hud_init(void);
void hud_draw(C3D_RenderTarget *bottom, const world *w, const player *p, const camera *c, float fps);
/* True when a touch at x,y (bottom-screen pixels) lands on the VIEW button. */
int hud_view_button_hit(int x, int y);
void hud_fini(void);

#endif
