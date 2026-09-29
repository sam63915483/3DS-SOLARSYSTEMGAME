// Pure sample processing: trimming, normalisation, sampler modes (length, rate, bits, loop,
// envelope) and the resample/quantise/crossfade pipeline. No Web Audio here, so Node can test it.

// ---- Modes -------------------------------------------------------------------------------
// Settings: seconds (max recording), rate (Hz the sample is stored at), bits, loop,
// attack/decay/release (ms), sustain (0..1).
export const MODE_PRESETS = [
  { name: 'USER', locked: false, desc: 'Editable. Starts as the default: full quality, looping',
    settings: { seconds: 10, rate: 32728, bits: 16, loop: true, attack: 4, decay: 0, sustain: 1, release: 40 } },
  { name: 'SK-1', locked: true, desc: '1.4 s, 9.4 kHz, 8-bit, one-shot, organ envelope: the real thing',
    settings: { seconds: 1.4, rate: 9380, bits: 8, loop: false, attack: 0, decay: 0, sustain: 1, release: 20 } },
  { name: 'DUSTY', locked: true, desc: '12-bit 26 kHz one-shot with a punchy decay, 80s drum-sampler grit',
    settings: { seconds: 5, rate: 26040, bits: 12, loop: false, attack: 0, decay: 400, sustain: 0.6, release: 150 } },
  { name: 'DRONE', locked: true, desc: 'Full quality, looping, slow swell and a long release for pads',
    settings: { seconds: 10, rate: 32728, bits: 16, loop: true, attack: 800, decay: 0, sustain: 1, release: 2000 } },
];

const ms = v => `${Math.round(v)} ms`;
export const MODE_FIELDS = [
  { key: 'seconds', label: 'LENGTH', choices: [1.4, 2, 5, 10], fmt: v => `${v} s` },
  { key: 'rate', label: 'RATE', choices: [8000, 9380, 16000, 22050, 26040, 32728], fmt: v => `${(v / 1000).toFixed(1)} kHz` },
  { key: 'bits', label: 'BITS', choices: [16, 12, 8, 4], fmt: v => `${v}-bit` },
  { key: 'loop', label: 'LOOP', choices: [true, false], fmt: v => (v ? 'ON' : 'OFF (one-shot)') },
  { key: 'attack', label: 'ATTACK', min: 0, max: 1000, fmt: ms },
  { key: 'decay', label: 'DECAY', min: 0, max: 2000, fmt: ms },
  { key: 'sustain', label: 'SUSTAIN', min: 0, max: 1, fmt: v => `${Math.round(v * 100)}%` },
  { key: 'release', label: 'RELEASE', min: 5, max: 3000, fmt: ms },
];

// Steps a field left (-1) or right (+1). Choice fields cycle through their list; slider fields
// move 2.5% of their range. Returns the new value.
export function stepField(field, value, dir) {
  if (field.choices) {
    const i = field.choices.indexOf(value);
    const n = Math.max(0, Math.min(field.choices.length - 1, (i < 0 ? 0 : i) + dir));
    return field.choices[n];
  }
  const step = (field.max - field.min) * 0.025;
  return Math.max(field.min, Math.min(field.max, value + dir * step));
}

// ---- Trimming ----------------------------------------------------------------------------
// Works on a 10 ms RMS envelope. The sound starts where the envelope first reaches a level
// relative to the loudest moment (with an absolute floor so faint noise never counts) and
// ends where it last stays above a lower "decay" level.
const ABS_FLOOR = 0.04;       // RMS below this is always silence (about -28 dBFS)
const START_REL = 0.15;       // start: within -16 dB of the loudest window
const END_REL = 0.03;         // end: last window within -30 dB of the loudest
const PRE_ROLL_MS = 10;
const TAIL_MS = 60;
const NORMALIZE_PEAK = 0.9;
const MAX_GAIN = 20;
const LOOP_FADE_MS = 50;
const START_FADE_MS = 2;

export function trimRange(data, rate) {
  const win = Math.round(rate / 100);
  const nw = Math.floor(data.length / win);
  if (nw < 1) return [0, data.length];
  const env = new Float32Array(nw);
  let peak = 0;
  for (let w = 0; w < nw; w++) {
    let sq = 0;
    for (let i = w * win; i < (w + 1) * win; i++) sq += data[i] * data[i];
    env[w] = Math.sqrt(sq / win);
    if (env[w] > peak) peak = env[w];
  }
  if (peak < ABS_FLOOR) return [0, data.length];
  const thrStart = Math.max(ABS_FLOOR, peak * START_REL);
  const thrEnd = Math.max(ABS_FLOOR * 0.5, peak * END_REL);
  let first = 0; while (first < nw && env[first] < thrStart) first++;
  let last = nw - 1; while (last > first && env[last] < thrEnd) last--;
  const start = Math.max(0, first * win - Math.round(rate * PRE_ROLL_MS / 1000));
  const end = Math.min(data.length, (last + 1) * win + Math.round(rate * TAIL_MS / 1000));
  return [start, end];
}

export function normalize(data) {
  let peak = 0;
  for (let i = 0; i < data.length; i++) { const a = Math.abs(data[i]); if (a > peak) peak = a; }
  if (peak < 1e-4) return data;
  const gain = Math.min(MAX_GAIN, NORMALIZE_PEAK / peak);
  for (let i = 0; i < data.length; i++) data[i] *= gain;
  return data;
}

// ---- Playback preparation ----------------------------------------------------------------

function startFade(out, rate) {
  const n = Math.min(Math.round(rate * START_FADE_MS / 1000), Math.floor(out.length / 8));
  for (let i = 0; i < n; i++) out[i] *= i / n;
  return out;
}

// Crossfades the tail into the head so looping has no audible seam. Playback runs 0..end
// once, then loops from `loopStart` (the head was already blended into the tail).
export function makeLoopable(data, rate) {
  const out = startFade(Float32Array.from(data), rate);
  const fade = Math.min(Math.round(rate * LOOP_FADE_MS / 1000), Math.floor(data.length / 4));
  const tailStart = data.length - fade;
  for (let i = 0; i < fade; i++) {
    const t = i / fade;
    out[tailStart + i] = data[tailStart + i] * Math.cos(t * Math.PI / 2) + data[i] * Math.sin(t * Math.PI / 2);
  }
  return { data: out, loopStart: fade };
}

// Resamples to the mode's rate (linear interpolation, aliasing and all: that's the lo-fi
// charm), reduces bit depth, then prepares the loop or the one-shot start.
export function processRecording(raw, rateIn, mode) {
  const ratio = rateIn / mode.rate;
  const n = Math.max(16, Math.floor(raw.length / ratio));
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const p = i * ratio, i0 = Math.min(raw.length - 1, Math.floor(p));
    const i1 = Math.min(raw.length - 1, i0 + 1), f = p - i0;
    out[i] = raw[i0] + (raw[i1] - raw[i0]) * f;
  }
  const prepared = mode.loop ? makeLoopable(out, mode.rate) : { data: startFade(out, mode.rate), loopStart: 0 };
  // Quantise last, so the stored data (fades included) really is N-bit like the hardware.
  if (mode.bits < 16) {
    const levels = Math.pow(2, mode.bits - 1);
    const d = prepared.data;
    for (let i = 0; i < d.length; i++) d[i] = Math.round(d[i] * levels) / levels;
  }
  return prepared;
}
