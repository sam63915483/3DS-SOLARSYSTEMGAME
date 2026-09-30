/* Orbit: a barebones explorable solar system with a third-person jetpack player. */
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include "world.h"
#include "player.h"
#include "camera.h"
#include "render.h"
#include "hud.h"
#include "flare.h"
#include "gimbal.h"

static float pad_axis(int v) {
    float a = v / 156.0f;
    if (a > 1) a = 1;
    if (a < -1) a = -1;
    return a;
}

/* Motion look. Sensor frame (matches Citra/Azahar, which mirror the console): x points right,
   y out of the screen towards the player, z down the screen towards the hinge, right-handed.
   Held like a window, tilting the top edge back is a positive rate about x (look up) and
   turning left is a positive rate about z... seen from below, so yaw = -z. Roll is ignored.
   HID hands back the coefficient as raw units per degree per second (14.375 on hardware and
   in Azahar), so divide. The raw gyro is uncalibrated: the first half second after switching
   it on is averaged as the zero point, and the zero keeps tracking slowly while the console is
   near still. Flip GYRO_PITCH_SIGN / GYRO_YAW_SIGN if a direction feels inverted on hardware. */
#define GYRO_GAIN        1.0f     /* camera degrees per console degree */
#define GYRO_DEADZONE    0.5f     /* degrees per second, hides sensor noise while still */
#define GYRO_MAX_RATE  300.0f     /* degrees per second, clamps glitches */
#define GYRO_STILL       3.0f     /* below this rate the zero point is allowed to drift along */
#define GYRO_CALIB_FRAMES 30
#define GYRO_PITCH_SIGN  1.0f
#define GYRO_YAW_SIGN    1.0f

static int sensors_on;
static float gyro_dps_per_raw = 1.0f / 14.375f;
static int calib_left;      /* frames of the initial hold-still average still to collect */
static v3 calib_sum, bias;  /* degrees per second */

static int sensors_calibrating(void) { return sensors_on && calib_left > 0; }

static void sensors_set(int on) {
    if (on == sensors_on) return;
    sensors_on = on;
    if (on) {
        HIDUSER_EnableGyroscope();
        float k;
        if (R_SUCCEEDED(HIDUSER_GetGyroscopeRawToDpsCoefficient(&k)) && k > 0)
            gyro_dps_per_raw = k > 1.0f ? 1.0f / k : k;   /* raw per dps on hardware; accept either */
        calib_left = GYRO_CALIB_FRAMES;
        calib_sum = bias = v3_(0, 0, 0);
    } else {
        HIDUSER_DisableGyroscope();
    }
}

static float deadzone(float v) { return fabsf(v) < GYRO_DEADZONE ? 0 : v; }
static float clampf(float v, float m) { return v > m ? m : (v < -m ? -m : v); }

static void sensors_read(float *pitch, float *yaw, float dt) {
    /* libctru's angularRate names its fields x, z, y in memory order; read the raw order */
    angularRate r; hidGyroRead(&r);
    const s16 *raw = (const s16 *)&r;
    v3 w = v3_scale(v3_(raw[0], raw[1], raw[2]), gyro_dps_per_raw);   /* deg/s about x, y, z */
    *pitch = *yaw = 0;
    if (calib_left > 0) {
        calib_sum = v3_add(calib_sum, w);
        if (--calib_left == 0) bias = v3_scale(calib_sum, 1.0f / GYRO_CALIB_FRAMES);
        return;
    }
    w = v3_sub(w, bias);
    if (v3_len(w) < GYRO_STILL) bias = v3_add(bias, v3_scale(w, ease(0.4f, dt)));   /* slow re-zero */
    float p = clampf(deadzone(w.x), GYRO_MAX_RATE);
    float y = clampf(deadzone(-w.z), GYRO_MAX_RATE);
    const float to_rad = 3.14159265f / 180.0f * GYRO_GAIN;
    *pitch = GYRO_PITCH_SIGN * p * to_rad;
    *yaw = GYRO_YAW_SIGN * y * to_rad;
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
        if (player_on_foot(&p)) {
            /* the pad looks around, A walks where the camera points, X jumps */
            cin.look_x = px; cin.look_y = py;
            in.mx = 0; in.my = (kHeld & KEY_A) ? 1.0f : 0.0f;
            in.jump_pressed = (kDown & KEY_X) != 0;
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
        in.view_up = cam.view_up;
        if (kDown & KEY_TOUCH) {
            touchPosition tp; hidTouchRead(&tp);
            if (hud_view_button_hit(tp.px, tp.py)) camera_toggle_view(&cam);
            else if (hud_gyro_button_hit(tp.px, tp.py, &cam)) camera_toggle_gyro(&cam);
        }
        sensors_set(cam.gyro);
        if (cam.gyro) sensors_read(&cin.gyro_pitch, &cin.gyro_yaw, dt);

        world_update(&w, dt);
        player_update(&p, &w, &in, dt);
        camera_update(&cam, &w, &p, &cin, dt);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        render_scene(&w, &p, &cam, w.t);
        C3D_LightEnvBind(NULL);
        C2D_Prepare();
        {
            float sx, sy, k = render_sun_screen(&sx, &sy);
            flare_draw(render_top_target(), sx, sy, k);
        }
        /* the 3D pass turns blending off; text and sprites need it back */
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
        gimbal_draw(render_top_target(), &w, &p, &cam);
        hud_draw(bottom, &w, &p, &cam, fps, sensors_calibrating());
        C2D_Flush();
        C3D_FrameEnd(0);
    }

    sensors_set(0);
    hud_fini();
    render_fini();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
