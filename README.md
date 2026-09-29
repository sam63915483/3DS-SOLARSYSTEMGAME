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

## Phase 2: 3DS homebrew port (plan)

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
