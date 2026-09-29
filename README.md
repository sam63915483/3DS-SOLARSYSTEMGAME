# SK-1 Clone for the original 3DS

A Casio SK-1 style sampling keyboard for a modded (CFW) original Nintendo 3DS.

## Phase 1: browser mockup (this folder, `mockup/`)

Runs at native 3DS resolution (top 400x240, bottom 320x240) so layout and touch
targets are honest about how they will feel on the real screens.

```
cd mockup
node serve.js
```

Open http://localhost:8000. The browser will ask for microphone permission the
first time you press REC.

Bottom screen (touch):
- `● REC` / `STOP` records the mic for up to 10 s. Stop early for a shorter sample.
- `+` / `−` zoom the keyboard in (fewer, bigger keys) and out (more, smaller keys).
- Slider under the keys scrolls the visible window across C2..C7.
- Drag across keys to glide. One touch at a time, like the resistive 3DS screen.

Top screen: oscilloscope visualizer while playing, live waveform while recording, the
finished sample after STOP (until the next key press). Effects tab bar along the bottom.

3DS buttons on the PC keyboard: arrows = D-pad, Z or Enter = A, X or Backspace = B,
WASD = circle pad (a USB gamepad's stick, D-pad and A/B also work). D-pad left/right
picks an effect tab, A opens it full screen, up/down moves between the power button, the
five presets and the sliders, left/right adjusts, A selects a preset or toggles power, B
closes. Effects stay on after closing and stack. D-pad up from the tab bar reaches the
keyboard preset button (top right): A opens a page with YOUR SAMPLE plus five synths
(Dream Pad, Glass Bells, Aurora, Supersaw, Wobble Bass). Recording a new sample always
switches back to YOUR SAMPLE. Circle pad: up/down bends pitch an octave, left/right
sweeps a low-pass / high-pass filter. The speakers are muted while recording.

Code map:
- `keyboard.js` key range, zoom, scroll, layout, hit-testing (pure math, ports to C).
- `sampler.js` Web Audio: built-in tone, mic recording, silence trim, loop crossfade,
  single voice with rate-based pitch, effect chain hookup, analyser for the scope.
- `effects.js` the five effects (distortion, chorus, flanger, delay, reverb), presets,
  slider ranges.
- `instruments.js` the five synthesized keyboard presets (oscillator voices).
- `fxui.js` D-pad navigation state and top-screen drawing (tab bar, effect panel,
  keyboard preset button and page).
- `screens.js` draws both screens at native resolution.
- `app.js` pointer handling, button mapping, and the render loop.

## Phase 2: 3DS homebrew port (`app/`)

Toolchain installed 2026-09-29 through MSYS2 (`C:\msys64`): devkitARM, libctru, citro2d,
3dslink, plus a host gcc for tests. Everything runs from an MSYS2 shell:

```
C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; cd /c/3ds/app && make"        # builds app/sk1.3dsx
C:\msys64\usr\bin\bash.exe -lc "cd /c/3ds/app && bash tests/run_tests.sh"          # host unit tests
```

Layout: `app/source/core/` is portable C (no 3DS headers) covering the keyboard model,
sample trim/loop, DSP helpers, the five effects, the six instruments, the mixer and the
D-pad menus. `app/source/*.c` is the thin 3DS layer (citro2d drawing, NDSP streaming,
mic capture, input, main loop). `app/tests/` runs the core on the PC.

Run on PC: Azahar emulator, File > Load File > `app/sk1.3dsx`. Two emulator details:
- libctru refuses to start audio without `sdmc:/3ds/dspfirm.cdc`. Azahar doesn't need the
  real file, so a 64 KB placeholder lives at `%APPDATA%\Azahar\sdmc\3ds\dspfirm.cdc`.
- Under Emulation > Configure > Audio pick the microphone. On this PC only the OpenAL
  real-device option delivers audio (`input_type=3` in `%APPDATA%\Azahar\config\qt-config.ini`,
  with `input_device=Microphone Array (AMD Audio Device)`); the cubeb option (1) stays
  silent and 2 is the static-noise generator. Verified 2026-09-29 by recording a speaker
  beep: the app trimmed the silence and kept the 0.96 s beep.

Package as a CIA (installs to the Home Menu like any other title, no Homebrew Launcher needed):

```
C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; cd /c/3ds/app && make && bash build_cia.sh"   # -> app/sk1.cia
```

Install over Wi-Fi with FBI: on the 3DS open FBI > Remote Install > Receive URLs over the
network (it shows the console's IP), then on the PC run `node tools/send_to_fbi.js <ip>`.
The script serves the CIA from this PC and FBI downloads and installs it. Or copy `sk1.cia`
to the SD card and install it with FBI from there. Reinstalling the same CIA updates it in
place. The DSP firmware note below still applies.

Run on the console through the Homebrew Launcher instead (both devices on the same Wi-Fi):
1. Once only: the audio needs the DSP firmware dump. Run the **DSP1** homebrew on the
   console once; it writes `sdmc:/3ds/dspfirm.cdc`. Without it the app shows a red
   message and stays silent.
2. Open the Homebrew Launcher, press **Y** (netloader); it shows the console's IP.
3. On the PC: `C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; 3dslink -a <ip> /c/3ds/app/sk1.3dsx"`
   The app starts on the console immediately. To keep it on the SD card instead, copy
   `sk1.3dsx` to `sdmc:/3ds/sk1/sk1.3dsx` (with ftpd or the card in a PC reader).

Controls on the console: touch = keys / REC / zoom / slider, D-pad + A/B = effects and
keyboard preset menus, circle pad = pitch bend (up/down) and filter sweep (left/right),
START = quit to the launcher.

## Port notes (kept from the plan)

Toolchain: devkitPro (devkitARM + libctru + citro2d/citro3d). Output is a `.3dsx`
run from the Homebrew Launcher on the modded console.

Mapping from the mockup to libctru:
| Mockup | 3DS |
| --- | --- |
| Canvas drawing | citro2d (`C2D_DrawRectSolid`, `C2D_DrawText`) |
| Pointer events | `hidScanInput()` + `hidTouchRead()` (single touch) |
| `getUserMedia` | `micInit()` / `MICU_StartSampling()` (16-bit PCM, 32728 Hz) |
| `AudioBufferSourceNode.playbackRate` | `ndspChnSetRate()` on one NDSP channel |
| `AudioContext.destination` | `ndspChnWaveBufAdd()` with a linear-memory buffer |

10 s of 16-bit mono at 32728 Hz is about 655 KB, well within the old 3DS's memory.

Effects on the 3DS: the DSP only mixes, resamples and pans, so the app will run its own
software mixer that fills NDSP wave buffers in blocks (e.g. 512 samples) and applies the
effects in C at 32 kHz mono. Delay, chorus, flanger and distortion are a few operations
per sample. Reverb must be algorithmic (Freeverb-style comb/allpass filters), not
convolution. All comfortably within the old 3DS's ARM11 budget for one voice.

Steps:
1. Install devkitPro on Windows (graphical installer), select the 3DS package group.
2. Build and run the `3ds/audio/mic` and `3ds/graphics/gpu/...` examples to confirm the toolchain.
3. Install Azahar (the maintained Citra fork) for desktop testing of the `.3dsx`.
4. Port `keyboard.js` to C first (pure logic), then the screens, then audio.
5. On the console: install ftpd or use the SD card; copy `sk1.3dsx` to `/3ds/`, launch from the Homebrew Launcher.
6. Later: wrap as a `.cia` so it appears on the Home Menu.

## Orbit: solar system jetpack demo (`orbit/`)

A second homebrew app, started 2026-09-29: a sun and three planets on circular orbits, a
third-person player with a jetpack, real inverse-square gravity from every body, no loading.
Design notes: `docs/superpowers/specs/2026-09-29-orbit-solar-system-design.md`.

```
C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; cd /c/3ds/orbit && make"                      # orbit/orbit.3dsx
C:\msys64\usr\bin\bash.exe -lc "cd /c/3ds/orbit && bash tests/run_tests.sh"                        # host tests of the sim
C:\msys64\usr\bin\bash.exe -lc "source /etc/profile; cd /c/3ds/orbit && make && bash build_cia.sh" # orbit/orbit.cia
node tools/send_to_fbi.js <3ds-ip> orbit/orbit.cia                                                   # install over Wi-Fi with FBI
```

Layout: `orbit/source/vec.h`, `world.c`, `player.c`, `camera.c`, `mesh.c` are portable C
(orbits, gravity, player physics, chase camera, procedural meshes) tested by
`orbit/tests/test_sim.c`. `render.c` is citro3d (vertex shader `vshader.v.pica`, PICA
fragment lighting with the sun as the point light), `hud.c` is the citro2d bottom screen,
`main.c` the loop.

Bottom screen: a VIEW touch button at the top right switches between third person and first
person with a smooth zoom into and out of the head. Text uses a built-in 5x7 pixel font
(`orbit/source/font.c`), so it renders in the emulator without a system font dump.

Controls on the ground: circle pad walks (camera relative, the player turns to face the
direction), X jumps, the D-pad looks around (left/right orbit, up/down pitch) and walking eases
the camera back behind you. In the air the game switches to flight controls:
X thrusts up and B thrusts down relative to your own body (the camera up), A boosts forward
along the view, so all three follow your orientation, not the planet; Y is an air brake, the circle pad is a free look with no limits (up/down
pitches, left/right turns, so you can loop), L/R roll. SELECT respawns, START quits. To
reach another planet: jump and hold X to climb out, look at the target planet with the pad
and hold A; near it turn around and hold A, or hold Y, to slow down before touching down. On landing the camera settles back to level.

Physics: the player's velocity is stored relative to the dominant body, so standing on a
moving planet is exact. Switching dominant body converts the velocity so world velocity is
continuous. Planet orbital speed is derived from the sun's gravity, so the sun's pull on the
player in a planet's frame is only the tidal remainder. Touching the sun respawns you.

Azahar notes: renders at 60 fps. Note for anyone mixing citro3d and citro2d: the 3D pass
sets an opaque blend mode and citro2d does not restore it, so re-enable alpha blending
before the 2D pass or every glyph and sprite draws as a solid block.
Azahar keys: arrows = circle pad, S = B, A = A, Z = X, X = Y, Q/W = L/R, N = SELECT, M = START.
