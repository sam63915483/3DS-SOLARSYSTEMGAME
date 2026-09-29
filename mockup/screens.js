// Drawing for both screens at native 3DS resolution: top 400x240, bottom 320x240.

import { noteName } from './keyboard.js';
import { WAVE_COLUMNS } from './sampler.js';
import { drawTabBar, drawFxPanel, drawHeader, drawPresetPage, drawModesPage, TAB_BAR } from './fxui.js';

// Bottom-screen layout. Touch targets are kept >= 36px tall for a stylus/finger.
export const BOTTOM = {
  rec:   { x: 8,   y: 6,   w: 84,  h: 36 },
  minus: { x: 228, y: 6,   w: 40,  h: 36 },
  plus:  { x: 272, y: 6,   w: 40,  h: 36 },
  keys:  { x: 0,   y: 48,  w: 320, h: 156 },
  track: { x: 8,   y: 212, w: 304, h: 22 },
};

export function hitRect(r, p) {
  return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

// Thumb rectangle of the scroll slider for the current keyboard state.
export function thumbRect(kb) {
  const t = BOTTOM.track;
  const w = Math.max(16, Math.round(t.w * kb.visibleWhite / kb.whiteCount));
  const x = Math.round(t.x + (t.w - w) * (kb.maxScroll ? kb.scroll / kb.maxScroll : 0));
  return { x, y: t.y, w, h: t.h };
}

function roundRect(g, x, y, w, h, r) {
  g.beginPath();
  g.moveTo(x + r, y);
  g.arcTo(x + w, y, x + w, y + h, r);
  g.arcTo(x + w, y + h, x, y + h, r);
  g.arcTo(x, y + h, x, y, r);
  g.arcTo(x, y, x + w, y, r);
  g.closePath();
}

function button(g, r, fill, label, textColor) {
  roundRect(g, r.x, r.y, r.w, r.h, 6);
  g.fillStyle = fill;
  g.fill();
  g.strokeStyle = '#55556a';
  g.lineWidth = 1;
  g.stroke();
  g.fillStyle = textColor;
  g.font = 'bold 14px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText(label, r.x + r.w / 2, r.y + r.h / 2 + 1);
}

export function drawBottom(g, state) {
  const { kb, sampler, heldMidi, drag } = state;
  g.fillStyle = '#202024';
  g.fillRect(0, 0, 320, 240);

  // Controls row.
  if (sampler.isRecording) button(g, BOTTOM.rec, '#c0392b', 'STOP', '#ffffff');
  else button(g, BOTTOM.rec, '#3a3a44', '● REC', '#ff6b5b');
  button(g, BOTTOM.minus, '#3a3a44', '−', '#ffffff');
  button(g, BOTTOM.plus, '#3a3a44', '+', '#ffffff');

  // Currently playing note, centred between the buttons, with the circle pad bend beside it.
  g.fillStyle = heldMidi === null ? '#3a3a44' : '#ffb347';
  g.font = 'bold 24px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText(heldMidi === null ? '–' : noteName(heldMidi), 160, 25);
  const bend = Math.round(sampler.bend);
  if (bend !== 0) {
    g.fillStyle = '#7ed957';
    g.font = 'bold 11px sans-serif';
    g.textAlign = 'left';
    g.fillText((bend > 0 ? '+' : '') + bend, 190, 25);
  }

  // Keyboard.
  const area = BOTTOM.keys;
  g.save();
  g.beginPath();
  g.rect(area.x, area.y, area.w, area.h);
  g.clip();
  const rects = kb.layout(area);
  g.lineWidth = 1;
  for (const r of rects) {
    const pressed = r.key.midi === heldMidi;
    if (r.key.black) {
      g.fillStyle = pressed ? '#ff9f43' : '#1a1a1e';
      g.fillRect(r.x, r.y, r.w, r.h);
      g.strokeStyle = '#000';
      g.strokeRect(r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
    } else {
      g.fillStyle = pressed ? '#ffb347' : '#f2f0ea';
      g.fillRect(r.x, r.y, r.w, r.h);
      g.strokeStyle = '#3a3a3a';
      g.strokeRect(r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
      if (r.key.midi % 12 === 0 && r.w >= 18) {
        g.fillStyle = '#6a6a6a';
        g.font = '10px sans-serif';
        g.textAlign = 'center';
        g.textBaseline = 'alphabetic';
        g.fillText(noteName(r.key.midi), r.x + r.w / 2, r.y + r.h - 6);
      }
    }
  }
  g.restore();

  // Scroll slider.
  const t = BOTTOM.track;
  roundRect(g, t.x, t.y, t.w, t.h, 5);
  g.fillStyle = '#111114';
  g.fill();
  g.strokeStyle = '#3a3a44';
  g.stroke();
  const th = thumbRect(kb);
  roundRect(g, th.x + 1, th.y + 2, th.w - 2, th.h - 4, 4);
  g.fillStyle = drag && drag.kind === 'slider' ? '#ff9f43' : '#6c6c7a';
  g.fill();
  g.strokeStyle = '#2a2a30';
  const cx = th.x + th.w / 2;
  for (const dx of [-4, 0, 4]) {
    g.beginPath();
    g.moveTo(cx + dx + 0.5, th.y + 7);
    g.lineTo(cx + dx + 0.5, th.y + th.h - 7);
    g.stroke();
  }
}

// --- Top screen -----------------------------------------------------------------------------
// Three modes, chosen by app.js: 'recording' (live mic waveform), 'sample' (what you recorded),
// 'scope' (visualizer of what is playing).

export function drawTop(g, state) {
  const { sampler, topMode, fx } = state;

  if (fx.presetOpen) {
    scopeStarted = false;
    drawPresetPage(g, fx);
    return;
  }
  if (fx.modesOpen) {
    scopeStarted = false;
    drawModesPage(g, fx);
    return;
  }
  if (fx.open) {
    scopeStarted = false;
    drawFxPanel(g, fx);
    return;
  }

  if (topMode === 'scope') {
    drawScope(g, sampler);
    drawTabBar(g, fx);
    drawHeader(g, fx);
    return;
  }
  scopeStarted = false;

  g.fillStyle = '#0e0f12';
  g.fillRect(0, 0, 400, 240);
  drawHeader(g, fx);

  // Waveform panel, leaving room for the effects tab bar underneath.
  const p = { x: 16, y: 40, w: WAVE_COLUMNS, h: TAB_BAR.y - 8 - 40 };
  roundRect(g, p.x, p.y, p.w, p.h, 4);
  g.fillStyle = '#15161b';
  g.fill();
  g.strokeStyle = '#2c2d36';
  g.lineWidth = 1;
  g.stroke();
  const mid = p.y + p.h / 2;
  g.beginPath();
  g.moveTo(p.x, mid + 0.5);
  g.lineTo(p.x + p.w, mid + 0.5);
  g.stroke();

  if (topMode === 'recording') {
    drawPeaks(g, p, sampler.livePeaks(), '#ff6b5b');
    const secs = sampler.recordedSeconds;
    g.fillStyle = '#c0392b';
    const maxSecs = sampler.mode.seconds;
    g.fillRect(p.x, p.y + p.h - 4, Math.round(p.w * secs / maxSecs), 4);
    g.fillStyle = '#ff6b5b';
    g.font = 'bold 12px sans-serif';
    g.textAlign = 'left';
    g.textBaseline = 'alphabetic';
    if (sampler.isArmed) {
      g.fillStyle = '#ffb347';
      g.fillText(`● ARMED  make a sound to start the ${maxSecs} s window`, p.x + 8, p.y + 16);
    } else {
      g.fillText(`● REC  ${secs.toFixed(1)} s / ${maxSecs} s`, p.x + 8, p.y + 16);
    }
    // Input level meter.
    const level = Math.min(1, sampler.inputLevel * 4);
    g.fillStyle = '#2c2d36';
    g.fillRect(p.x + p.w - 88, p.y + 8, 80, 8);
    g.fillStyle = level > 0.9 ? '#ff3b3b' : '#7ed957';
    g.fillRect(p.x + p.w - 88, p.y + 8, Math.round(80 * level), 8);
  } else if (sampler.sample) {
    drawPeaks(g, p, sampler.peaks(), '#ffb347');
    g.fillStyle = '#8a8a96';
    g.font = '11px sans-serif';
    g.textAlign = 'left';
    g.textBaseline = 'alphabetic';
    const trim = sampler.trimmedMs && !sampler.sampleIsBuiltIn ? `   (trimmed ${sampler.trimmedMs} ms of silence)` : '';
    const label = sampler.sampleIsBuiltIn ? 'BUILT-IN TONE' : 'YOUR SAMPLE';
    const m = sampler.mode;
    g.fillText(`${label}  ${sampler.sampleSeconds.toFixed(2)} s   ${(m.rate / 1000).toFixed(1)} kHz ${m.bits}-bit ${m.loop ? 'loop' : 'one-shot'}${trim}`, p.x + 8, p.y + 16);
    g.fillStyle = '#6a6a78';
    g.textAlign = 'right';
    g.fillText('PLAY A KEY FOR THE SCOPE', p.x + p.w - 8, p.y + p.h - 10);
  }

  drawTabBar(g, fx);
  drawError(g, sampler);
}

function drawError(g, sampler) {
  if (!sampler.error) return;
  g.fillStyle = '#ff3b3b';
  g.font = 'bold 11px sans-serif';
  g.textAlign = 'left';
  g.textBaseline = 'alphabetic';
  g.fillText(sampler.error.slice(0, 60), 16, TAB_BAR.y - 10);
}

function drawPeaks(g, p, peaks, color) {
  if (!peaks) return;
  const mid = p.y + p.h / 2;
  const amp = p.h / 2 - 8;
  g.fillStyle = color;
  for (let c = 0; c < WAVE_COLUMNS; c++) {
    const top = Math.round(mid - peaks.max[c] * amp);
    const bottom = Math.round(mid - peaks.min[c] * amp);
    g.fillRect(p.x + c, top, 1, Math.max(1, bottom - top));
  }
}

// --- Oscilloscope visualizer -------------------------------------------------------------
// Colour follows the note (one hue per pitch class), size follows loudness. Frames are not
// cleared fully, so shapes leave fading trails.

const W = 400, H = 240, CX = 200, CY = 128;
const BAR_COUNT = 48;
let scopeStarted = false;

function drawScope(g, sampler) {
  if (!scopeStarted) {
    g.fillStyle = '#0e0f12';
    g.fillRect(0, 0, W, H);
    scopeStarted = true;
  }
  // Persistence: dim the previous frame instead of wiping it.
  g.fillStyle = 'rgba(14, 15, 18, 0.28)';
  g.fillRect(0, 0, W, H);

  const snap = sampler.scope();
  const rms = snap ? snap.rms : 0;
  const midi = snap && snap.midi !== null ? snap.midi : null;
  const hue = midi === null ? 200 : (midi % 12) * 30;
  const loud = Math.min(1, rms * 3);
  const active = rms > 0.003;

  // Frequency bars, mirrored across the centre line, faint so the waveform reads on top.
  if (snap && active) {
    const usable = Math.floor(snap.freq.length * 0.5); // ignore the very top of the spectrum
    const per = usable / BAR_COUNT;
    const bw = W / BAR_COUNT;
    for (let b = 0; b < BAR_COUNT; b++) {
      let peak = 0;
      const start = Math.floor(b * per), end = Math.floor((b + 1) * per);
      for (let i = start; i < end; i++) if (snap.freq[i] > peak) peak = snap.freq[i];
      const hgt = (peak / 255) * 100;
      if (hgt < 1) continue;
      g.fillStyle = `hsla(${(hue + b * 4) % 360}, 80%, 55%, 0.35)`;
      g.fillRect(Math.round(b * bw) + 1, Math.round(CY - hgt), Math.round(bw) - 2, Math.round(hgt * 2));
    }
  }

  // Radial shape: the waveform wrapped around a circle whose size follows loudness.
  const baseR = 26 + loud * 46;
  g.lineWidth = 1.5;
  g.strokeStyle = `hsla(${(hue + 180) % 360}, 85%, 65%, ${active ? 0.9 : 0.25})`;
  g.beginPath();
  const N = 180;
  for (let i = 0; i <= N; i++) {
    const a = (i / N) * Math.PI * 2 - Math.PI / 2;
    const s = snap && active ? snap.wave[Math.floor(((i % N) / N) * snap.wave.length)] : 0;
    const r = baseR + s * 60;
    const x = CX + Math.cos(a) * r, y = CY + Math.sin(a) * r;
    if (i === 0) g.moveTo(x, y); else g.lineTo(x, y);
  }
  g.closePath();
  g.stroke();

  // Main trace across the full width.
  g.lineWidth = 2;
  g.strokeStyle = `hsl(${hue}, 90%, ${active ? 65 : 35}%)`;
  g.shadowColor = `hsl(${hue}, 90%, 60%)`;
  g.shadowBlur = active ? 10 : 0;
  g.beginPath();
  if (snap && active) {
    const step = snap.wave.length / W;
    for (let x = 0; x < W; x++) {
      const y = CY - snap.wave[Math.floor(x * step)] * 90;
      if (x === 0) g.moveTo(x, y); else g.lineTo(x, y);
    }
  } else {
    g.moveTo(0, CY);
    g.lineTo(W, CY);
  }
  g.stroke();
  g.shadowBlur = 0;

  // Note name under the title (the header row itself is drawn by the caller).
  g.textBaseline = 'alphabetic';
  g.textAlign = 'center';
  g.fillStyle = midi === null ? '#3a3a44' : `hsl(${hue}, 90%, 70%)`;
  g.font = 'bold 18px sans-serif';
  g.fillText(midi === null ? '–' : noteName(midi), 200, 48);

  drawError(g, sampler);
}
