/* Orbit: a barebones explorable solar system with a third-person jetpack player. */
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include "world.h"
#include "player.h"
#include "camera.h"
#include "render.h"
#include "hud.h"

static float pad_axis(int v) {
    float a = v / 156.0f;
    if (a > 1) a = 1;
    if (a < -1) a = -1;
    return a;
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    world w; world_init(&w);
    player p; player_spawn(&p, &w, BODY_START);
    camera cam; camera_init(&cam, &p);
    render_init(&w);
    hud_init();

    u64 last = osGetTime();
    float fps = 60;
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown(), kHeld = hidKeysHeld();
        if (kDown & KEY_START) break;
        if (kDown & KEY_SELECT) { player_spawn(&p, &w, BODY_START); camera_init(&cam, &p); }

        u64 now = osGetTime();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt < 1.0f / 120) dt = 1.0f / 120;
        if (dt > 1.0f / 20) dt = 1.0f / 20;
        fps += ((1.0f / dt) - fps) * 0.05f;

        circlePosition cp; hidCircleRead(&cp);
        player_input in;
        memset(&in, 0, sizeof in);
        in.mx = pad_axis(cp.dx);
        in.my = pad_axis(cp.dy);
        float mag = sqrtf(in.mx * in.mx + in.my * in.my);
        if (mag < 0.18f) in.mx = in.my = 0;
        in.jump_pressed = (kDown & KEY_B) != 0;
        in.thrust_up = (kHeld & KEY_B) != 0;
        in.thrust_down = (kHeld & KEY_A) != 0;
        in.brake = (kHeld & KEY_Y) != 0;
        in.cam_forward = cam.fwd;
        float turn = ((kHeld & KEY_R) ? 1.0f : 0.0f) - ((kHeld & KEY_L) ? 1.0f : 0.0f);

        world_update(&w, dt);
        player_update(&p, &w, &in, dt);
        camera_update(&cam, &w, &p, turn, dt);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        render_scene(&w, &p, &cam);
        C3D_LightEnvBind(NULL);
        C2D_Prepare();
        hud_draw(bottom, &w, &p, fps);
        C2D_Flush();
        C3D_FrameEnd(0);
    }

    hud_fini();
    render_fini();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
