/* SK-1 Clone for the 3DS: entry point and main loop. */
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include "app.h"
#include "draw.h"
#include "platform_audio.h"
#include "platform_mic.h"
#include "ui_bottom.h"
#include "ui_top.h"

static App app;   /* ~2 MB: keep it static */

/* D-pad with auto-repeat: 20 frames initial delay, then every 4 frames. */
static const u32 DPAD_KEYS[6] = {KEY_DLEFT, KEY_DRIGHT, KEY_DUP, KEY_DDOWN, KEY_A, KEY_B};
static int heldFrames[6];

static void handle_buttons(void) {
    u32 held = hidKeysHeld(), down = hidKeysDown();
    for (int i = 0; i < 6; i++) {
        int press = 0, repeat = 0;
        if (down & DPAD_KEYS[i]) { press = 1; heldFrames[i] = 0; }
        else if (held & DPAD_KEYS[i]) { heldFrames[i]++; if (heldFrames[i] >= 20 && (heldFrames[i] - 20) % 4 == 0) { press = 1; repeat = 1; } }
        else heldFrames[i] = 0;
        if (!press) continue;
        int changed;
        int r = fxui_key(&app.fxui, i, repeat, &changed);
        if (r & FXUI_CHANGED_FX) mixer_apply_fx(&app.mixer, changed, &app.fxui.cfg[changed]);
        if (r & FXUI_CHANGED_INST) mixer_set_instrument(&app.mixer, app.fxui.instrument);
        if (r & FXUI_CHANGED_MODE) { mixer_set_mode(&app.mixer, fxui_mode(&app.fxui)); app_compute_sample_peaks(&app); app.heldMidi = -1; }
    }
}

static void handle_circle_pad(void) {
    circlePosition cp; hidCircleRead(&cp);
    float x = cp.dx / 150.0f, y = cp.dy / 150.0f;
    x = clampf(x, -1, 1);
    y = clampf(y, -1, 1);
    if (x > -0.15f && x < 0.15f) x = 0;
    if (y > -0.15f && y < 0.15f) y = 0;
    static float lx = 0, ly = 0;
    if (x != lx || y != ly) { mixer_set_pad(&app.mixer, x, y); lx = x; ly = y; }
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    draw_init();

    memset(&app, 0, sizeof(app));
    mixer_init(&app.mixer);
    kb_init(&app.kb);
    fxui_init(&app.fxui);
    app.heldMidi = -1;
    app.topMode = TOP_SCOPE;
    app_compute_sample_peaks(&app);
    mixer_set_mode(&app.mixer, fxui_mode(&app.fxui));
    app.dspOk = audio_init();
    app.micOk = mic_init();

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        handle_buttons();
        handle_circle_pad();
        ui_bottom_input(&app);

        int wasRecording = mic_is_recording();
        mic_pump(&app.mixer);
        if (mic_is_recording()) app.topMode = TOP_RECORDING;
        else if (wasRecording || app.topMode == TOP_RECORDING) {
            /* recording just ended (STOP or the 10 s cap) */
            mixer_set_muted(&app.mixer, 0);
            app.fxui.instrument = INST_SAMPLE;
            mixer_set_instrument(&app.mixer, INST_SAMPLE);
            app_compute_sample_peaks(&app);
            app.topMode = TOP_SAMPLE;
        }

        audio_pump(&app.mixer);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        draw_frame_begin();
        C2D_TargetClear(top, RGB(0x0e, 0x0f, 0x12));
        C2D_SceneBegin(top);
        ui_top_draw(&app);
        C2D_TargetClear(bot, RGB(0x20, 0x20, 0x24));
        C2D_SceneBegin(bot);
        ui_bottom_draw(&app);
        C3D_FrameEnd(0);

        audio_pump(&app.mixer);   /* twice per frame keeps the queue full even on slow frames */
    }

    mic_exit();
    audio_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
