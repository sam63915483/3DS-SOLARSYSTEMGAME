// Effects UI on the top screen: a tab bar along the bottom, and a full-screen panel per effect.
// Driven entirely by D-pad / circle pad + A + B so the touch screen stays free for playing.

import { EFFECTS, toNorm, fromNorm, formatValue } from './effects.js';
import { INSTRUMENTS } from './instruments.js';

const SLIDER_STEP = 0.025; // fraction of slider travel per D-pad press

export function createFxState() {
  const per = {};
  for (const fx of EFFECTS) {
    const p = fx.presets[0];
    per[fx.id] = { enabled: false, preset: 0, mode: p.mode || 'normal', values: { ...p.values } };
  }
  return {
    tab: 0,        // highlighted tab in the bar
    open: false,   // is an effect panel open
    row: 0,        // focus in panel: 0 = power, 1 = presets, 2.. = sliders
    col: 0,        // highlighted preset when row === 1
    per,
    // Keyboard preset button (top right) and its page.
    onPresetButton: false, // D-pad focus is on the button instead of the tab bar
    presetOpen: false,     // the keyboard preset page is showing
    presetRow: 0,          // highlighted instrument on the page
    instrument: 'sample',  // active instrument id
  };
}

export function instrumentName(id) {
  const inst = INSTRUMENTS.find(i => i.id === id);
  return inst ? inst.name : id;
}

export function fxConfig(state, id) {
  const s = state.per[id];
  return { enabled: s.enabled, mode: s.mode, values: { ...s.values } };
}

// key: 'left' | 'right' | 'up' | 'down' | 'a' | 'b'. Returns true if the key was used.
// onChange(id) fires whenever an effect's audio settings changed.
// onInstrument(id) fires when a keyboard preset is chosen.
export function handleFxKey(state, key, repeat, onChange, onInstrument) {
  const fx = EFFECTS[state.tab];
  const s = state.per[fx.id];

  if (state.presetOpen) {
    if (repeat) return true;
    if (key === 'up') state.presetRow = Math.max(0, state.presetRow - 1);
    else if (key === 'down') state.presetRow = Math.min(INSTRUMENTS.length - 1, state.presetRow + 1);
    else if (key === 'a') {
      state.instrument = INSTRUMENTS[state.presetRow].id;
      onInstrument(state.instrument);
    } else if (key === 'b') state.presetOpen = false;
    return true;
  }

  if (!state.open) {
    if (repeat) return false;
    if (state.onPresetButton) {
      if (key === 'down') state.onPresetButton = false;
      else if (key === 'a') {
        state.presetOpen = true;
        state.presetRow = Math.max(0, INSTRUMENTS.findIndex(i => i.id === state.instrument));
      }
      return true;
    }
    if (key === 'up') { state.onPresetButton = true; return true; }
    if (key === 'left') { state.tab = (state.tab + EFFECTS.length - 1) % EFFECTS.length; return true; }
    if (key === 'right') { state.tab = (state.tab + 1) % EFFECTS.length; return true; }
    if (key === 'a') { state.open = true; state.row = 0; state.col = s.preset; return true; }
    return false;
  }

  const lastRow = 1 + fx.params.length;
  const sliderIndex = state.row - 2;

  switch (key) {
    case 'b':
      if (!repeat) state.open = false;
      return true;
    case 'up':
      if (!repeat) state.row = Math.max(0, state.row - 1);
      return true;
    case 'down':
      if (!repeat) state.row = Math.min(lastRow, state.row + 1);
      return true;
    case 'a':
      if (repeat) return true;
      if (state.row === 0) { s.enabled = !s.enabled; onChange(fx.id); }
      else if (state.row === 1) applyPreset(state, fx, state.col, onChange);
      return true;
    case 'left':
    case 'right': {
      const dir = key === 'left' ? -1 : 1;
      if (state.row === 0) {
        if (!repeat) { s.enabled = !s.enabled; onChange(fx.id); }
      } else if (state.row === 1) {
        if (!repeat) state.col = Math.max(0, Math.min(fx.presets.length - 1, state.col + dir));
      } else {
        const p = fx.params[sliderIndex];
        s.values[p.key] = fromNorm(p, toNorm(p, s.values[p.key]) + dir * SLIDER_STEP);
        onChange(fx.id);
      }
      return true;
    }
  }
  return false;
}

function applyPreset(state, fx, index, onChange) {
  const s = state.per[fx.id];
  const p = fx.presets[index];
  s.preset = index;
  s.mode = p.mode || 'normal';
  s.values = { ...p.values };
  onChange(fx.id);
}

function isTweaked(fx, s) {
  const p = fx.presets[s.preset];
  return fx.params.some(param => Math.abs(p.values[param.key] - s.values[param.key]) > 1e-6);
}

// ---- Drawing ----------------------------------------------------------------------------

export const TAB_BAR = { y: 212, h: 24 };

function roundRect(g, x, y, w, h, r) {
  g.beginPath();
  g.moveTo(x + r, y);
  g.arcTo(x + w, y, x + w, y + h, r);
  g.arcTo(x + w, y + h, x, y + h, r);
  g.arcTo(x, y + h, x, y, r);
  g.arcTo(x, y, x + w, y, r);
  g.closePath();
}

function box(g, r, fill, border, borderWidth) {
  roundRect(g, r.x, r.y, r.w, r.h, 5);
  g.fillStyle = fill;
  g.fill();
  g.strokeStyle = border;
  g.lineWidth = borderWidth;
  g.stroke();
}

// Keyboard preset button in the top-right corner of the top screen.
export const PRESET_BUTTON = { x: 262, y: 7, w: 130, h: 22 };

export function drawPresetButton(g, state) {
  const r = PRESET_BUTTON;
  const focused = state.onPresetButton && !state.open && !state.presetOpen;
  box(g, r, focused ? '#2c2d36' : '#15161b', focused ? '#ffb347' : '#3a3a44', focused ? 2 : 1);
  g.fillStyle = focused ? '#ffffff' : '#c8c8d2';
  g.font = 'bold 11px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText('♪ PRESET', r.x + r.w / 2, r.y + r.h / 2 + 1);
}

export function drawPresetPage(g, state) {
  g.fillStyle = '#0e0f12';
  g.fillRect(0, 0, 400, 240);
  g.textBaseline = 'alphabetic';
  g.textAlign = 'left';
  g.fillStyle = '#ff6b5b';
  g.font = 'bold 16px sans-serif';
  g.fillText('KEYBOARD PRESET', 16, 24);

  INSTRUMENTS.forEach((inst, i) => {
    const r = { x: 16, y: 36 + i * 30, w: 368, h: 26 };
    const selected = inst.id === state.instrument;
    const focused = i === state.presetRow;
    box(g, r, selected ? '#5a3a14' : '#1a1a20', focused ? '#ffb347' : (selected ? '#a86a2a' : '#2c2d36'), focused ? 2 : 1);
    g.textBaseline = 'middle';
    g.textAlign = 'left';
    g.fillStyle = selected ? '#ffb347' : (focused ? '#ffffff' : '#c8c8d2');
    g.font = 'bold 11px sans-serif';
    g.fillText((selected ? '● ' : '') + inst.name, r.x + 10, r.y + r.h / 2 + 1);
    g.fillStyle = focused ? '#c8c8d2' : '#6a6a78';
    g.font = '10px sans-serif';
    g.textAlign = 'right';
    g.fillText(inst.desc, r.x + r.w - 10, r.y + r.h / 2 + 1);
  });

  g.fillStyle = '#4a4a56';
  g.font = '10px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'alphabetic';
  g.fillText('▲▼ move   A select   B close', 200, 232);
}

export function drawTabBar(g, state) {
  const n = EFFECTS.length;
  const slot = 384 / n;
  const w = Math.round(slot) - 4;
  g.fillStyle = '#0e0f12';
  g.fillRect(0, TAB_BAR.y - 4, 400, 240 - TAB_BAR.y + 4);
  EFFECTS.forEach((fx, i) => {
    const r = { x: Math.round(8 + i * slot), y: TAB_BAR.y, w, h: TAB_BAR.h };
    const focused = i === state.tab && !state.onPresetButton;
    const on = state.per[fx.id].enabled;
    box(g, r, focused ? '#2c2d36' : '#15161b', focused ? '#ffb347' : '#2c2d36', focused ? 2 : 1);
    if (on) {
      g.fillStyle = '#7ed957';
      g.beginPath();
      g.arc(r.x + 9, r.y + r.h / 2, 3, 0, Math.PI * 2);
      g.fill();
    }
    g.fillStyle = on ? '#7ed957' : (focused ? '#ffffff' : '#9a9aa6');
    g.font = 'bold 11px sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText(fx.name, r.x + r.w / 2 + (on ? 4 : 0), r.y + r.h / 2 + 1);
  });
}

export function drawFxPanel(g, state) {
  const fx = EFFECTS[state.tab];
  const s = state.per[fx.id];
  g.fillStyle = '#0e0f12';
  g.fillRect(0, 0, 400, 240);

  // Title.
  g.textBaseline = 'alphabetic';
  g.textAlign = 'left';
  g.fillStyle = '#ff6b5b';
  g.font = 'bold 16px sans-serif';
  g.fillText(fx.name, 16, 24);
  g.fillStyle = '#6a6a78';
  g.font = '11px sans-serif';
  const presetName = fx.presets[s.preset].name + (isTweaked(fx, s) ? ' (tweaked)' : '');
  g.fillText(presetName, 16 + g.measureText(fx.name).width + 30, 24);
  g.textAlign = 'right';
  g.fillStyle = s.enabled ? '#7ed957' : '#6a6a78';
  g.font = 'bold 12px sans-serif';
  g.fillText(s.enabled ? 'EFFECT ON' : 'EFFECT OFF', 384, 24);

  // Row 0: power button.
  const power = { x: 16, y: 34, w: 96, h: 26 };
  const focus0 = state.row === 0;
  box(g, power, s.enabled ? '#1f4d2a' : '#2a2a32', focus0 ? '#ffb347' : '#3a3a44', focus0 ? 2 : 1);
  g.fillStyle = s.enabled ? '#7ed957' : '#9a9aa6';
  g.font = 'bold 12px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText(s.enabled ? '● ON' : '○ OFF', power.x + power.w / 2, power.y + power.h / 2 + 1);

  // Row 1: presets.
  const slot = 368 / fx.presets.length;
  fx.presets.forEach((p, i) => {
    const r = { x: Math.round(16 + i * slot), y: 68, w: Math.round(slot) - 4, h: 26 };
    const selected = i === s.preset;
    const focused = state.row === 1 && i === state.col;
    box(g, r, selected ? '#5a3a14' : '#1a1a20', focused ? '#ffb347' : (selected ? '#a86a2a' : '#2c2d36'), focused ? 2 : 1);
    g.fillStyle = selected ? '#ffb347' : (focused ? '#ffffff' : '#9a9aa6');
    g.font = 'bold 10px sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText(p.name, r.x + r.w / 2, r.y + r.h / 2 + 1);
  });

  // Rows 2..: sliders.
  fx.params.forEach((p, i) => {
    const y = 110 + i * 28;
    const focused = state.row === 2 + i;
    const t = toNorm(p, s.values[p.key]);
    g.textBaseline = 'middle';
    g.textAlign = 'left';
    g.fillStyle = focused ? '#ffffff' : '#9a9aa6';
    g.font = focused ? 'bold 11px sans-serif' : '11px sans-serif';
    g.fillText(p.label, 16, y);
    const track = { x: 96, y: y - 5, w: 224, h: 10 };
    roundRect(g, track.x, track.y, track.w, track.h, 5);
    g.fillStyle = '#1a1a20';
    g.fill();
    g.strokeStyle = focused ? '#ffb347' : '#2c2d36';
    g.lineWidth = 1;
    g.stroke();
    const fillW = Math.round(track.w * t);
    if (fillW > 0) {
      roundRect(g, track.x, track.y, fillW, track.h, 5);
      g.fillStyle = focused ? '#ffb347' : '#6c6c7a';
      g.fill();
    }
    g.fillStyle = focused ? '#ffffff' : '#c8c8d2';
    g.beginPath();
    g.arc(track.x + fillW, y, 6, 0, Math.PI * 2);
    g.fill();
    g.textAlign = 'right';
    g.fillStyle = focused ? '#ffb347' : '#c8c8d2';
    g.font = '11px sans-serif';
    g.fillText(formatValue(p, s.values[p.key]), 384, y);
  });

  // Footer hint.
  g.fillStyle = '#4a4a56';
  g.font = '10px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'alphabetic';
  g.fillText('▲▼ move   ◄► adjust   A select   B close', 200, 232);
}
