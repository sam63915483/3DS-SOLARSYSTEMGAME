// Top-screen menus, driven by D-pad + A + B so the touch screen stays free for playing.
// Every page is one vertical list: up/down moves through rows (headers are skipped),
// left/right adjusts the focused row, A selects, B goes back. Six rows fit on screen and
// the list scrolls to keep the focused row visible. Text is sized for the 3DS's 400x240.

import { EFFECTS, toNorm, fromNorm, formatValue } from './effects.js';
import { INSTRUMENTS } from './instruments.js';
import { MODE_PRESETS, MODE_FIELDS, stepField } from './sample.js';

const SLIDER_STEP = 0.025; // fraction of slider travel per D-pad press
export const VISIBLE_ROWS = 6;

export function createFxState() {
  const per = {};
  for (const fx of EFFECTS) {
    const p = fx.presets[0];
    per[fx.id] = { enabled: false, preset: 0, mode: p.mode || 'normal', values: { ...p.values } };
  }
  return {
    tab: 0,               // highlighted tab in the bar
    open: false,          // effect panel open
    row: 1, scroll: 0,    // focused row (index into the page's rows) and first visible row
    per,
    onTopRow: false,      // D-pad focus on the MODES / PRESET buttons
    topCol: 1,            // 0 = MODES, 1 = PRESET
    presetOpen: false, presetRow: 1, presetScroll: 0,
    instrument: 'sample',
    modesOpen: false, modesRow: 1, modesScroll: 0,
    modePreset: 0,        // active mode preset (0 = USER, editable)
    userMode: { ...MODE_PRESETS[0].settings },
    loopOverride: null,   // LOOP button on the bottom screen: null = follow the preset
  };
}

export function fxConfig(state, id) {
  const s = state.per[id];
  return { enabled: s.enabled, mode: s.mode, values: { ...s.values } };
}

export function currentMode(state) {
  const base = state.modePreset === 0 ? state.userMode : MODE_PRESETS[state.modePreset].settings;
  return state.loopOverride === null ? base : { ...base, loop: state.loopOverride };
}

// Bottom-screen LOOP button: flips looping for the current sound, even on locked presets.
export function toggleLoop(state) {
  const next = !currentMode(state).loop;
  if (state.modePreset === 0) { state.userMode.loop = next; state.loopOverride = null; }
  else state.loopOverride = next;
}

export function instrumentName(id) {
  const inst = INSTRUMENTS.find(i => i.id === id);
  return inst ? inst.name : id;
}

// ---- Row models -------------------------------------------------------------------------

function effectRows(state) {
  const fx = EFFECTS[state.tab];
  const s = state.per[fx.id];
  const rows = [
    { type: 'header', label: 'EFFECT' },
    { type: 'toggle', label: 'POWER', on: s.enabled },
    { type: 'header', label: 'PRESETS' },
  ];
  fx.presets.forEach((p, i) => rows.push({ type: 'preset', label: p.name, selected: i === s.preset, index: i }));
  rows.push({ type: 'header', label: 'SETTINGS' });
  fx.params.forEach((p, i) => rows.push({ type: 'slider', label: p.label, t: toNorm(p, s.values[p.key]), value: formatValue(p, s.values[p.key]), editable: true, index: i }));
  return rows;
}

function modesRows(state) {
  const locked = MODE_PRESETS[state.modePreset].locked;
  const mode = currentMode(state);
  const rows = [{ type: 'header', label: 'PRESET' }];
  MODE_PRESETS.forEach((p, i) => rows.push({ type: 'preset', label: p.name + (p.locked ? '' : ' (editable)'), selected: i === state.modePreset, desc: p.desc, index: i }));
  rows.push({ type: 'header', label: locked ? 'SETTINGS  (fixed: select USER to edit)' : 'SETTINGS' });
  MODE_FIELDS.forEach((f, i) => {
    const value = mode[f.key];
    if (f.choices) {
      const idx = f.choices.indexOf(value);
      rows.push({ type: 'choice', label: f.label, value: f.fmt(value), canLeft: idx > 0, canRight: idx < f.choices.length - 1, editable: !locked, index: i });
    } else {
      rows.push({ type: 'slider', label: f.label, t: (value - f.min) / (f.max - f.min), value: f.fmt(value), editable: !locked, index: i });
    }
  });
  return rows;
}

function presetRows(state) {
  const rows = [{ type: 'header', label: 'KEYBOARD SOUND' }];
  INSTRUMENTS.forEach((inst, i) => rows.push({ type: 'preset', label: inst.name, selected: inst.id === state.instrument, desc: inst.desc, index: i }));
  return rows;
}

function moveFocus(rows, row, dir) {
  let i = row + dir;
  while (i >= 0 && i < rows.length && rows[i].type === 'header') i += dir;
  return i >= 0 && i < rows.length ? i : row;
}

function ensureVisible(rows, focus, scroll) {
  // keep the header above the focused row visible when it sits right above it
  let top = focus;
  if (top > 0 && rows[top - 1].type === 'header') top--;
  if (top < scroll) return top;
  if (focus >= scroll + VISIBLE_ROWS) return focus - VISIBLE_ROWS + 1;
  return Math.min(scroll, Math.max(0, rows.length - VISIBLE_ROWS));
}

// What to draw for the open page: title, right-hand text, rows, focus, scroll, status line.
export function pageView(state) {
  if (state.presetOpen) {
    const rows = presetRows(state);
    const r = rows[state.presetRow];
    return { title: 'KEYBOARD PRESET', right: 'A select   B back', rows, focus: state.presetRow, scroll: state.presetScroll, status: r && r.desc ? r.desc : '' };
  }
  if (state.modesOpen) {
    const rows = modesRows(state);
    const r = rows[state.modesRow];
    const preset = MODE_PRESETS[state.modePreset];
    const status = r && r.desc ? r.desc : (preset.locked ? `${preset.name} is fixed. Select USER to edit the settings` : 'USER: left/right changes any setting');
    return { title: 'MODES', right: `${preset.name}   A select   B back`, rows, focus: state.modesRow, scroll: state.modesScroll, status };
  }
  if (state.open) {
    const fx = EFFECTS[state.tab];
    const s = state.per[fx.id];
    const rows = effectRows(state);
    const tweaked = fx.params.some(p => Math.abs(fx.presets[s.preset].values[p.key] - s.values[p.key]) > 1e-6);
    return { title: fx.name, right: `${fx.presets[s.preset].name}${tweaked ? ' (tweaked)' : ''}   ${s.enabled ? 'ON' : 'OFF'}`, rows, focus: state.row, scroll: state.scroll, status: 'A toggles power or picks a preset   left/right adjusts sliders   B back' };
  }
  return null;
}

// ---- Input ------------------------------------------------------------------------------

// key: 'left' | 'right' | 'up' | 'down' | 'a' | 'b'. Returns true if the key was used.
// onChange(id) fires when an effect's audio settings changed, onInstrument(id) when a keyboard
// preset is chosen, onMode() when the sampler mode changed.
export function handleFxKey(state, key, repeat, onChange, onInstrument, onMode = () => {}) {
  const fx = EFFECTS[state.tab];
  const s = state.per[fx.id];

  if (state.presetOpen) {
    const rows = presetRows(state);
    if (key === 'up' || key === 'down') state.presetRow = moveFocus(rows, state.presetRow, key === 'up' ? -1 : 1);
    else if (key === 'a' && !repeat) { state.instrument = INSTRUMENTS[rows[state.presetRow].index].id; onInstrument(state.instrument); }
    else if (key === 'b' && !repeat) state.presetOpen = false;
    state.presetScroll = ensureVisible(rows, state.presetRow, state.presetScroll);
    return true;
  }

  if (state.modesOpen) {
    const rows = modesRows(state);
    const r = rows[state.modesRow];
    if (key === 'up' || key === 'down') state.modesRow = moveFocus(rows, state.modesRow, key === 'up' ? -1 : 1);
    else if (key === 'b') { if (!repeat) state.modesOpen = false; }
    else if (r.type === 'preset') {
      if (key === 'a' && !repeat) { state.modePreset = r.index; state.loopOverride = null; onMode(); }
    } else if (r.editable && (key === 'left' || key === 'right')) {
      const field = MODE_FIELDS[r.index];
      if (field.choices && repeat) return true;
      state.userMode[field.key] = stepField(field, state.userMode[field.key], key === 'left' ? -1 : 1);
      if (field.key === 'loop') state.loopOverride = null;
      onMode();
    }
    state.modesScroll = ensureVisible(rows, state.modesRow, state.modesScroll);
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
          const idx = Math.max(0, INSTRUMENTS.findIndex(i => i.id === state.instrument));
          state.presetRow = 1 + idx; state.presetScroll = ensureVisible(presetRows(state), state.presetRow, 0);
        } else {
          state.modesOpen = true;
          state.modesRow = 1 + state.modePreset; state.modesScroll = 0;
        }
      }
      return true;
    }
    if (key === 'up') { state.onTopRow = true; return true; }
    if (key === 'left') { state.tab = (state.tab + EFFECTS.length - 1) % EFFECTS.length; return true; }
    if (key === 'right') { state.tab = (state.tab + 1) % EFFECTS.length; return true; }
    if (key === 'a') { state.open = true; state.row = 1; state.scroll = 0; return true; }
    return false;
  }

  const rows = effectRows(state);
  const r = rows[state.row];
  if (key === 'up' || key === 'down') state.row = moveFocus(rows, state.row, key === 'up' ? -1 : 1);
  else if (key === 'b') { if (!repeat) state.open = false; }
  else if (r.type === 'toggle') {
    if (!repeat && (key === 'a' || key === 'left' || key === 'right')) { s.enabled = !s.enabled; onChange(fx.id); }
  } else if (r.type === 'preset') {
    if (key === 'a' && !repeat) applyPreset(state, fx, r.index, onChange);
  } else if (r.type === 'slider' && (key === 'left' || key === 'right')) {
    const p = fx.params[r.index];
    s.values[p.key] = fromNorm(p, toNorm(p, s.values[p.key]) + (key === 'left' ? -1 : 1) * SLIDER_STEP);
    onChange(fx.id);
  }
  state.scroll = ensureVisible(rows, state.row, state.scroll);
  return true;
}

function applyPreset(state, fx, index, onChange) {
  const s = state.per[fx.id];
  const p = fx.presets[index];
  s.preset = index;
  s.mode = p.mode || 'normal';
  s.values = { ...p.values };
  onChange(fx.id);
}

// ---- Drawing ----------------------------------------------------------------------------

export const TAB_BAR = { y: 212, h: 24 };
export const MODES_BUTTON = { x: 8, y: 7, w: 100, h: 22 };
export const PRESET_BUTTON = { x: 292, y: 7, w: 100, h: 22 };
const ROW_H = 30, ROWS_TOP = 36;

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
  g.font = 'bold 12px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText(label, r.x + r.w / 2, r.y + r.h / 2 + 1);
}

// The header row shared by every non-page view: MODES, the title, PRESET.
export function drawHeader(g, state) {
  const focusable = state.onTopRow && !state.open && !state.presetOpen && !state.modesOpen;
  topButton(g, MODES_BUTTON, 'MODES', focusable && state.topCol === 0);
  topButton(g, PRESET_BUTTON, '♪ PRESET', focusable && state.topCol === 1);
  g.fillStyle = '#ff6b5b';
  g.font = 'bold 14px sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.fillText('SK-1 CLONE', 200, 18);
}

function drawRow(g, r, y, focused) {
  const dim = r.editable === false;
  if (focused) box(g, { x: 12, y: y + 2, w: 372, h: ROW_H - 4 }, '#2c2d36', '#ffb347', 2);
  g.textBaseline = 'middle';
  const mid = y + ROW_H / 2 + 1;
  if (r.type === 'header') {
    g.fillStyle = '#6a6a78';
    g.font = 'bold 11px sans-serif';
    g.textAlign = 'left';
    g.fillText(r.label, 16, mid + 4);
    g.fillStyle = '#2c2d36';
    g.fillRect(12, y + ROW_H - 2, 372, 1);
    return;
  }
  const labelColor = focused ? '#ffffff' : (dim ? '#5a5a66' : '#c8c8d2');
  if (r.type === 'toggle') {
    g.fillStyle = labelColor; g.font = 'bold 15px sans-serif'; g.textAlign = 'left';
    g.fillText(r.label, 26, mid);
    g.fillStyle = r.on ? '#7ed957' : '#9a9aa6'; g.textAlign = 'right';
    g.fillText(r.on ? '● ON' : '○ OFF', 372, mid);
  } else if (r.type === 'preset') {
    g.fillStyle = r.selected ? '#ffb347' : labelColor; g.font = 'bold 15px sans-serif'; g.textAlign = 'left';
    g.fillText((r.selected ? '● ' : '   ') + r.label, 26, mid);
  } else if (r.type === 'choice') {
    g.fillStyle = labelColor; g.font = '14px sans-serif'; g.textAlign = 'left';
    g.fillText(r.label, 26, mid);
    g.fillStyle = focused ? '#ffb347' : (dim ? '#6a6a78' : '#c8c8d2'); g.font = 'bold 15px sans-serif'; g.textAlign = 'center';
    const left = r.editable && r.canLeft ? '◄  ' : '     ';
    const right = r.editable && r.canRight ? '  ►' : '     ';
    g.fillText(left + r.value + right, 250, mid);
  } else if (r.type === 'slider') {
    g.fillStyle = labelColor; g.font = '14px sans-serif'; g.textAlign = 'left';
    g.fillText(r.label, 26, mid);
    const track = { x: 150, y: mid - 6, w: 150, h: 12 };
    roundRect(g, track.x, track.y, track.w, track.h, 6);
    g.fillStyle = '#1a1a20'; g.fill();
    g.strokeStyle = focused ? '#ffb347' : '#2c2d36'; g.lineWidth = 1; g.stroke();
    const fillW = Math.round(track.w * r.t);
    if (fillW > 0) { roundRect(g, track.x, track.y, fillW, track.h, 6); g.fillStyle = focused ? '#ffb347' : (dim ? '#3a3a44' : '#6c6c7a'); g.fill(); }
    g.fillStyle = focused ? '#ffffff' : (dim ? '#6a6a78' : '#c8c8d2');
    g.beginPath(); g.arc(track.x + fillW, mid, 7, 0, Math.PI * 2); g.fill();
    g.fillStyle = focused ? '#ffb347' : (dim ? '#6a6a78' : '#c8c8d2'); g.font = 'bold 14px sans-serif'; g.textAlign = 'right';
    g.fillText(r.value, 372, mid);
  }
}

export function drawPage(g, view) {
  g.fillStyle = '#0e0f12';
  g.fillRect(0, 0, 400, 240);
  g.textBaseline = 'middle';
  g.fillStyle = '#ff6b5b';
  g.font = 'bold 18px sans-serif';
  g.textAlign = 'left';
  g.fillText(view.title, 16, 17);
  g.fillStyle = '#6a6a78';
  g.font = '12px sans-serif';
  g.textAlign = 'right';
  g.fillText(view.right, 384, 18);

  const { rows } = view;
  const end = Math.min(rows.length, view.scroll + VISIBLE_ROWS);
  for (let i = view.scroll; i < end; i++) drawRow(g, rows[i], ROWS_TOP + (i - view.scroll) * ROW_H, i === view.focus);

  if (rows.length > VISIBLE_ROWS) {
    const trackH = VISIBLE_ROWS * ROW_H;
    g.fillStyle = '#1a1a20';
    g.fillRect(392, ROWS_TOP, 4, trackH);
    const thumbH = Math.max(12, trackH * VISIBLE_ROWS / rows.length);
    const thumbY = ROWS_TOP + (trackH - thumbH) * view.scroll / (rows.length - VISIBLE_ROWS);
    g.fillStyle = '#6c6c7a';
    g.fillRect(392, thumbY, 4, thumbH);
  }

  g.fillStyle = '#8a8a96';
  g.font = '12px sans-serif';
  g.textAlign = 'center';
  g.fillText(view.status, 200, 228);
}

// Compatibility wrappers used by screens.js.
export function drawPresetPage(g, state) { drawPage(g, pageView(state)); }
export function drawModesPage(g, state) { drawPage(g, pageView(state)); }
export function drawFxPanel(g, state) { drawPage(g, pageView(state)); }

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
    g.font = 'bold 12px sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText(fx.name, r.x + r.w / 2 + (on ? 4 : 0), r.y + r.h / 2 + 1);
  });
}
