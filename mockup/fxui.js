// Top-screen menus, driven by D-pad + A + B so the touch screen stays free for playing:
// the effects tab bar and panel, the MODES button/page (top left) and the PRESET
// button/page (top right).

import { EFFECTS, toNorm, fromNorm, formatValue } from './effects.js';
import { INSTRUMENTS } from './instruments.js';
import { MODE_PRESETS, MODE_FIELDS, stepField } from './sample.js';

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
    // Top row of buttons: D-pad up from the tab bar focuses it; left/right picks MODES or PRESET.
    onTopRow: false,
    topCol: 1,             // 0 = MODES, 1 = PRESET
    // Keyboard preset page.
    presetOpen: false,
    presetRow: 0,
    instrument: 'sample',
    // Modes page.
    modesOpen: false,
    modesRow: 0,           // 0 = preset row, 1.. = fields
    modesCol: 0,           // highlighted mode preset when modesRow === 0
    modePreset: 0,         // active mode preset (0 = USER, editable)
    userMode: { ...MODE_PRESETS[0].settings },
  };
}

export function fxConfig(state, id) {
  const s = state.per[id];
  return { enabled: s.enabled, mode: s.mode, values: { ...s.values } };
}

export function currentMode(state) {
  return state.modePreset === 0 ? state.userMode : MODE_PRESETS[state.modePreset].settings;
}

export function instrumentName(id) {
  const inst = INSTRUMENTS.find(i => i.id === id);
  return inst ? inst.name : id;
}

// key: 'left' | 'right' | 'up' | 'down' | 'a' | 'b'. Returns true if the key was used.
// onChange(id) fires when an effect's audio settings changed, onInstrument(id) when a keyboard
// preset is chosen, onMode() when the sampler mode changed.
export function handleFxKey(state, key, repeat, onChange, onInstrument, onMode = () => {}) {
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

  if (state.modesOpen) {
    const lastRow = MODE_FIELDS.length;
    const locked = MODE_PRESETS[state.modePreset].locked;
    if (key === 'b') { if (!repeat) state.modesOpen = false; return true; }
    if (key === 'up') { if (!repeat) state.modesRow = Math.max(0, state.modesRow - 1); return true; }
    if (key === 'down') { if (!repeat) state.modesRow = Math.min(lastRow, state.modesRow + 1); return true; }
    if (state.modesRow === 0) {
      if (repeat) return true;
      if (key === 'left') state.modesCol = Math.max(0, state.modesCol - 1);
      else if (key === 'right') state.modesCol = Math.min(MODE_PRESETS.length - 1, state.modesCol + 1);
      else if (key === 'a') { state.modePreset = state.modesCol; onMode(); }
      return true;
    }
    if (locked) return true;   // only USER is editable
    const field = MODE_FIELDS[state.modesRow - 1];
    if (key === 'left' || key === 'right') {
      if (field.choices && repeat) return true;
      state.userMode[field.key] = stepField(field, state.userMode[field.key], key === 'left' ? -1 : 1);
      onMode();
    }
    return true;
  }

  if (!state.open) {
    if (repeat) return false;
    if (state.onTopRow) {
      if (key === 'down') state.onTopRow = false;
      else if (key === 'left') state.topCol = 0;
      else if (key === 'right') state.topCol = 1;
      else if (key === 'a') {
        if (state.topCol === 1) {
          state.presetOpen = true;
          state.presetRow = Math.max(0, INSTRUMENTS.findIndex(i => i.id === state.instrument));
        } else {
          state.modesOpen = true;
          state.modesRow = 0;
          state.modesCol = state.modePreset;
        }
      }
      return true;
    }
    if (key === 'up') { state.onTopRow = true; return true; }
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
export const MODES_BUTTON = { x: 8, y: 7, w: 100, h: 22 };
export const PRESET_BUTTON = { x: 292, y: 7, w: 100, h: 22 };

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

function topButton(g, r, label, focused) {
  box(g, r, focused ? '#2c2d36' : '#15161b', focused ? '#ffb347' : '#3a3a44', focused ? 2 : 1);
  g.fillStyle = focused ? '#ffffff' : '#c8c8d2';
  g.font = 'bold 11px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText(label, r.x + r.w / 2, r.y + r.h / 2 + 1);
}

// The header row shared by every non-page view: MODES, the title, PRESET.
export function drawHeader(g, state) {
  const focusable = state.onTopRow && !state.open && !state.presetOpen && !state.modesOpen;
  topButton(g, MODES_BUTTON, '⚙ MODES', focusable && state.topCol === 0);
  topButton(g, PRESET_BUTTON, '♪ PRESET', focusable && state.topCol === 1);
  g.fillStyle = '#ff6b5b';
  g.font = 'bold 14px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText('SK-1 CLONE', 200, 18);
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

export function drawModesPage(g, state) {
  const preset = MODE_PRESETS[state.modePreset];
  const mode = currentMode(state);
  g.fillStyle = '#0e0f12';
  g.fillRect(0, 0, 400, 240);
  g.textBaseline = 'alphabetic';
  g.textAlign = 'left';
  g.fillStyle = '#ff6b5b';
  g.font = 'bold 16px sans-serif';
  g.fillText('MODES', 16, 24);
  g.fillStyle = '#6a6a78';
  g.font = '11px sans-serif';
  g.textAlign = 'right';
  g.fillText(preset.locked ? `${preset.name} is fixed. Select USER to edit settings` : 'USER: edit any setting', 384, 24);

  // Mode preset row.
  const slot = 368 / MODE_PRESETS.length;
  MODE_PRESETS.forEach((p, i) => {
    const r = { x: Math.round(16 + i * slot), y: 32, w: Math.round(slot) - 4, h: 24 };
    const selected = i === state.modePreset;
    const focused = state.modesRow === 0 && i === state.modesCol;
    box(g, r, selected ? '#5a3a14' : '#1a1a20', focused ? '#ffb347' : (selected ? '#a86a2a' : '#2c2d36'), focused ? 2 : 1);
    g.fillStyle = selected ? '#ffb347' : (focused ? '#ffffff' : '#9a9aa6');
    g.font = 'bold 11px sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText((selected ? '● ' : '') + p.name + (p.locked ? '' : ' ✎'), r.x + r.w / 2, r.y + r.h / 2 + 1);
  });
  g.fillStyle = '#6a6a78';
  g.font = '10px sans-serif';
  g.textAlign = 'left';
  g.textBaseline = 'middle';
  g.fillText(MODE_PRESETS[state.modesRow === 0 ? state.modesCol : state.modePreset].desc, 16, 66);

  // Settings rows.
  MODE_FIELDS.forEach((f, i) => {
    const y = 84 + i * 18;
    const focused = state.modesRow === 1 + i;
    const editable = !preset.locked;
    const value = mode[f.key];
    g.textBaseline = 'middle';
    g.textAlign = 'left';
    g.fillStyle = focused ? '#ffffff' : (editable ? '#9a9aa6' : '#5a5a66');
    g.font = focused ? 'bold 11px sans-serif' : '11px sans-serif';
    g.fillText(f.label, 16, y);
    const track = { x: 96, y: y - 5, w: 224, h: 10 };
    if (f.choices) {
      const idx = f.choices.indexOf(value);
      g.fillStyle = focused ? '#ffb347' : (editable ? '#c8c8d2' : '#6a6a78');
      g.font = 'bold 11px sans-serif';
      g.textAlign = 'center';
      const left = idx > 0 && editable ? '◄  ' : '    ';
      const right = idx < f.choices.length - 1 && editable ? '  ►' : '    ';
      g.fillText(left + f.fmt(value) + right, track.x + track.w / 2, y);
    } else {
      const t = (value - f.min) / (f.max - f.min);
      roundRect(g, track.x, track.y, track.w, track.h, 5);
      g.fillStyle = '#1a1a20';
      g.fill();
      g.strokeStyle = focused ? '#ffb347' : '#2c2d36';
      g.lineWidth = 1;
      g.stroke();
      const fillW = Math.round(track.w * t);
      if (fillW > 0) {
        roundRect(g, track.x, track.y, fillW, track.h, 5);
        g.fillStyle = focused ? '#ffb347' : (editable ? '#6c6c7a' : '#3a3a44');
        g.fill();
      }
      g.fillStyle = focused ? '#ffffff' : (editable ? '#c8c8d2' : '#6a6a78');
      g.beginPath();
      g.arc(track.x + fillW, y, 5, 0, Math.PI * 2);
      g.fill();
      g.textAlign = 'right';
      g.fillStyle = focused ? '#ffb347' : (editable ? '#c8c8d2' : '#6a6a78');
      g.font = '11px sans-serif';
      g.fillText(f.fmt(value), 384, y);
    }
  });

  g.fillStyle = '#4a4a56';
  g.font = '10px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'alphabetic';
  g.fillText('▲▼ move   ◄► change   A select preset   B close', 200, 234);
}

export function drawTabBar(g, state) {
  const n = EFFECTS.length;
  const slot = 384 / n;
  const w = Math.round(slot) - 4;
  g.fillStyle = '#0e0f12';
  g.fillRect(0, TAB_BAR.y - 4, 400, 240 - TAB_BAR.y + 4);
  EFFECTS.forEach((fx, i) => {
    const r = { x: Math.round(8 + i * slot), y: TAB_BAR.y, w, h: TAB_BAR.h };
    const focused = i === state.tab && !state.onTopRow;
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
