// Wires input to the models and runs the render loop.
// The bottom canvas behaves like the 3DS touch screen: one pointer at a time, in 320x240 coordinates.

import { KeyboardModel } from './keyboard.js';
import { Sampler } from './sampler.js';
import { drawTop, drawBottom, BOTTOM, hitRect, thumbRect } from './screens.js';
import { createFxState, fxConfig, handleFxKey, currentMode, toggleLoop } from './fxui.js';

const shell = document.getElementById('shell');
const topCanvas = document.getElementById('top');
const bottomCanvas = document.getElementById('bottom');
const topG = topCanvas.getContext('2d');
const bottomG = bottomCanvas.getContext('2d');

const state = {
  kb: new KeyboardModel(),
  sampler: new Sampler(),
  heldMidi: null,
  drag: null, // { kind: 'key' | 'slider', pointerId, grabOffset }
  // Top screen: 'scope' (visualizer) until REC is pressed, 'recording' while the mic runs,
  // 'sample' after STOP until the first key press switches back to 'scope'.
  topMode: 'scope',
  fx: createFxState(),
};

// --- Sizing: scale the native screens up in half-steps so the mockup fits the window. ---
function fitScale() {
  const nativeW = 400 + 2 * 20 + 4;
  const nativeH = 2 * (240 + 2 * 14 + 4) + 6;
  const s = Math.min((innerWidth - 32) / nativeW, (innerHeight - 70) / nativeH);
  const snapped = Math.max(1, Math.floor(s * 2) / 2);
  document.documentElement.style.setProperty('--s', snapped);
  shell.classList.toggle('pixel', Number.isInteger(snapped));
}
addEventListener('resize', fitScale);
fitScale();

// --- Touch handling. ---
function toNative(e) {
  const r = bottomCanvas.getBoundingClientRect();
  return { x: (e.clientX - r.left) * 320 / r.width, y: (e.clientY - r.top) * 240 / r.height };
}

function pressAt(p) {
  const key = state.kb.hit(BOTTOM.keys, p.x, p.y);
  if (!key) { releaseNote(); return; }
  if (key.midi === state.heldMidi) return;
  state.heldMidi = key.midi;
  state.sampler.noteOn(key.midi);
  if (!state.sampler.isRecording) state.topMode = 'scope';
}

function releaseNote() {
  if (state.heldMidi === null) return;
  state.heldMidi = null;
  state.sampler.noteOff();
}

function sliderTo(px, grabOffset) {
  const t = BOTTOM.track;
  const th = thumbRect(state.kb);
  const usable = t.w - th.w;
  const frac = usable > 0 ? (px - grabOffset - t.x) / usable : 0;
  state.kb.setScroll(frac * state.kb.maxScroll);
}

function toggleRecord() {
  const s = state.sampler;
  if (s.isRecording) s.stopRecording();
  else s.startRecording();
}

bottomCanvas.addEventListener('pointerdown', (e) => {
  if (state.drag) return; // resistive touch screen: only one touch at a time
  e.preventDefault();
  state.sampler.ensure();
  const p = toNative(e);

  if (hitRect(BOTTOM.rec, p)) { toggleRecord(); return; }
  if (hitRect(BOTTOM.loop, p)) { toggleLoop(state.fx); onMode(); return; }
  if (hitRect(BOTTOM.minus, p)) { state.kb.zoomOut(); return; }
  if (hitRect(BOTTOM.plus, p)) { state.kb.zoomIn(); return; }

  if (hitRect(BOTTOM.track, p)) {
    const th = thumbRect(state.kb);
    const grabOffset = hitRect(th, p) ? p.x - th.x : th.w / 2;
    state.drag = { kind: 'slider', pointerId: e.pointerId, grabOffset };
    bottomCanvas.setPointerCapture(e.pointerId);
    sliderTo(p.x, grabOffset);
    return;
  }

  if (hitRect(BOTTOM.keys, p)) {
    state.drag = { kind: 'key', pointerId: e.pointerId };
    bottomCanvas.setPointerCapture(e.pointerId);
    pressAt(p);
  }
});

bottomCanvas.addEventListener('pointermove', (e) => {
  const d = state.drag;
  if (!d || d.pointerId !== e.pointerId) return;
  const p = toNative(e);
  if (d.kind === 'key') pressAt(p);
  else sliderTo(p.x, d.grabOffset);
});

function endDrag(e) {
  const d = state.drag;
  if (!d || d.pointerId !== e.pointerId) return;
  if (d.kind === 'key') releaseNote();
  state.drag = null;
}
bottomCanvas.addEventListener('pointerup', endDrag);
bottomCanvas.addEventListener('pointercancel', endDrag);

// --- 3DS buttons on the PC keyboard. Arrows = D-pad (effects UI), Z or Enter = A,
// X or Backspace = B. WASD = circle pad (held = full deflection). A USB gamepad also works:
// its D-pad, A/B and left stick map the same way.
const BUTTONS = {
  ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down',
  Enter: 'a', KeyZ: 'a', Backspace: 'b', KeyX: 'b',
};
const PAD_KEYS = { KeyW: [0, 1], KeyS: [0, -1], KeyA: [-1, 0], KeyD: [1, 0] };
const padKeysDown = new Set();

function onFxChange(id) {
  state.sampler.applyFx(id, fxConfig(state.fx, id));
}

function onInstrument(id) {
  state.sampler.setInstrument(id);
}

function onMode() {
  state.sampler.setMode(currentMode(state.fx));
}

function pressButton(button, repeat) {
  handleFxKey(state.fx, button, repeat, onFxChange, onInstrument, onMode);
}
onMode();

addEventListener('keydown', (e) => {
  const button = BUTTONS[e.code] || BUTTONS[e.key];
  if (button) {
    e.preventDefault();
    pressButton(button, e.repeat);
    return;
  }
  if (PAD_KEYS[e.code]) { padKeysDown.add(e.code); return; }
  if (e.repeat) return;
  // Desktop-only shortcuts (not part of the 3DS design).
  switch (e.key) {
    case ' ': e.preventDefault(); state.sampler.ensure(); toggleRecord(); break;
    case '+': case '=': state.kb.zoomIn(); break;
    case '-': case '_': state.kb.zoomOut(); break;
  }
});

addEventListener('keyup', (e) => padKeysDown.delete(e.code));
addEventListener('blur', () => padKeysDown.clear());

// --- Gamepad: D-pad and A/B produce the same button events as the keyboard, with key repeat
// for held directions. The left stick is the circle pad.
const GP_BUTTONS = { 12: 'up', 13: 'down', 14: 'left', 15: 'right', 0: 'a', 1: 'b' };
const gpHeld = {}; // button index -> { since, lastRepeat }
const REPEAT_DELAY = 350, REPEAT_RATE = 60;

function pollGamepad(now) {
  const pads = navigator.getGamepads ? navigator.getGamepads() : [];
  const gp = Array.from(pads).find(p => p && p.connected);
  if (!gp) return null;
  for (const [index, button] of Object.entries(GP_BUTTONS)) {
    const pressed = gp.buttons[index] && gp.buttons[index].pressed;
    const held = gpHeld[index];
    if (pressed && !held) {
      gpHeld[index] = { since: now, lastRepeat: now };
      pressButton(button, false);
    } else if (pressed && now - held.since > REPEAT_DELAY && now - held.lastRepeat > REPEAT_RATE) {
      held.lastRepeat = now;
      pressButton(button, true);
    } else if (!pressed && held) {
      delete gpHeld[index];
    }
  }
  const dead = v => (Math.abs(v) < 0.15 ? 0 : v);
  return { x: dead(gp.axes[0] || 0), y: -dead(gp.axes[1] || 0) };
}

// Circle pad position from whichever source is deflected more.
function readCirclePad(now) {
  let x = 0, y = 0;
  for (const code of padKeysDown) { x += PAD_KEYS[code][0]; y += PAD_KEYS[code][1]; }
  const gp = pollGamepad(now);
  if (gp && Math.hypot(gp.x, gp.y) > Math.hypot(x, y)) ({ x, y } = gp);
  return { x: Math.max(-1, Math.min(1, x)), y: Math.max(-1, Math.min(1, y)) };
}

let lastPad = { x: 0, y: 0 };

// --- Render loop. ---
function frame(now) {
  const pad = readCirclePad(now || 0);
  if (pad.x !== lastPad.x || pad.y !== lastPad.y) {
    state.sampler.setPad(pad.x, pad.y);
    lastPad = pad;
  }
  // Recording can also stop by itself at the 10 s cap, so derive the mode change here.
  if (state.sampler.isRecording) state.topMode = 'recording';
  else if (state.topMode === 'recording') {
    state.topMode = 'sample';
    state.fx.instrument = state.sampler.instrument; // a new recording selects YOUR SAMPLE
  }
  drawTop(topG, state);
  drawBottom(bottomG, state);
  requestAnimationFrame(frame);
}
frame();
