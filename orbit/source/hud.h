/* Bottom-screen HUD drawn with citro2d and the built-in pixel font. */
#ifndef HUD_H
#define HUD_H
#include <citro2d.h>
#include "world.h"
#include "player.h"
#include "camera.h"
#include "fishing.h"

void hud_init(void);
/* gyro_settling: the gyro was just switched on and is averaging its zero point; ask to hold still */
void hud_draw(C3D_RenderTarget *bottom, const world *w, const player *p, const camera *c, const fishing *f, float fps, int gyro_settling);
/* True when a touch at x,y (bottom-screen pixels) lands on the VIEW button. */
int hud_view_button_hit(int x, int y);
/* True when a touch lands on the GYRO button, which only exists in first person. */
int hud_gyro_button_hit(int x, int y, const camera *c);
void hud_fini(void);

#endif
