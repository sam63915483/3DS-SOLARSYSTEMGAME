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
        float px = pad_axis(cp.dx), py = pad_axis(cp.dy);
        if (sqrtf(px * px + py * py) < 0.18f) px = py = 0;

        player_input in;
        memset(&in, 0, sizeof in);
        camera_input cin;
        memset(&cin, 0, sizeof cin);
        if (p.grounded) {
            /* walk with the pad, X jumps, d-pad looks around */
            in.mx = px; in.my = py;
            in.jump_pressed = (kDown & KEY_X) != 0;
            cin.turn = ((kHeld & KEY_DRIGHT) ? 1.0f : 0.0f) - ((kHeld & KEY_DLEFT) ? 1.0f : 0.0f);
            cin.look_y = ((kHeld & KEY_DUP) ? 1.0f : 0.0f) - ((kHeld & KEY_DDOWN) ? 1.0f : 0.0f);
        } else {
            /* flight: the pad looks around freely, L/R roll, buttons thrust */
            cin.turn = ((kHeld & KEY_R) ? 1.0f : 0.0f) - ((kHeld & KEY_L) ? 1.0f : 0.0f);
            cin.look_x = px; cin.look_y = py;
            in.thrust_up = (kHeld & KEY_X) != 0;
            in.thrust_fwd = (kHeld & KEY_A) != 0;
            in.thrust_down = (kHeld & KEY_B) != 0;
            in.brake = (kHeld & KEY_Y) != 0;
            in.roll = cin.turn;
        }
        in.cam_forward = cam.fwd;
        in.view_forward = cam.view;
        if (kDown & KEY_TOUCH) {
            touchPosition tp; hidTouchRead(&tp);
            if (hud_view_button_hit(tp.px, tp.py)) camera_toggle_view(&cam);
        }

        world_update(&w, dt);
        player_update(&p, &w, &in, dt);
        camera_update(&cam, &w, &p, &cin, dt);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        render_scene(&w, &p, &cam, w.t);
        C3D_LightEnvBind(NULL);
        C2D_Prepare();
        /* the 3D pass turns blending off; text and sprites need it back */
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
        hud_draw(bottom, &w, &p, &cam, fps);
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
