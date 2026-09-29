// Five insert effects and their presets. Chain order is fixed:
//   DISTORTION -> CHORUS -> FLANGER -> DELAY -> REVERB
// Each module has `input`/`output` nodes and `configure({ enabled, mode, values })`.
// When disabled the dry path passes through untouched, so effects can be switched at any time.

// ---- Parameter and preset definitions ---------------------------------------------------
// `log: true` makes the slider travel logarithmic (good for Hz). `keepDry` means the dry signal
// stays at full level and `mix` only sets the wet amount (delay/reverb), otherwise mix crossfades.

export const CHAIN_ORDER = ['distortion', 'chorus', 'flanger', 'delay', 'reverb'];

const DEFINITIONS = [
  {
    id: 'reverb', name: 'REVERB', keepDry: true,
    params: [
      { key: 'size', label: 'SIZE', min: 0.2, max: 6, unit: 's' },
      { key: 'damp', label: 'DAMP', min: 500, max: 12000, unit: 'Hz', log: true },
      { key: 'pre', label: 'PREDELAY', min: 0, max: 120, unit: 'ms' },
      { key: 'mix', label: 'MIX', min: 0, max: 1, unit: '%' },
    ],
    presets: [
      { name: 'ROOM', mode: 'normal', values: { size: 0.7, damp: 6000, pre: 5, mix: 0.3 } },
      { name: 'HALL', mode: 'normal', values: { size: 2.5, damp: 4000, pre: 20, mix: 0.4 } },
      { name: 'CATHEDRAL', mode: 'normal', values: { size: 6, damp: 3000, pre: 40, mix: 0.5 } },
      { name: 'PLATE', mode: 'normal', values: { size: 1.5, damp: 11000, pre: 0, mix: 0.45 } },
      { name: 'REVERSE', mode: 'reverse', values: { size: 1.5, damp: 7000, pre: 0, mix: 0.8 } },
    ],
  },
  {
    id: 'flanger', name: 'FLANGER', keepDry: false,
    params: [
      { key: 'rate', label: 'RATE', min: 0.05, max: 5, unit: 'hz', log: true },
      { key: 'depth', label: 'DEPTH', min: 0.2, max: 8, unit: 'ms' },
      { key: 'feedback', label: 'FEEDBACK', min: 0, max: 0.9, unit: '%' },
      { key: 'mix', label: 'MIX', min: 0, max: 1, unit: '%' },
    ],
    presets: [
      { name: 'CLASSIC', values: { rate: 0.3, depth: 2, feedback: 0.5, mix: 0.5 } },
      { name: 'JET', values: { rate: 0.12, depth: 5, feedback: 0.8, mix: 0.6 } },
      { name: 'METALLIC', values: { rate: 2.5, depth: 1, feedback: 0.85, mix: 0.6 } },
      { name: 'SLOW SWEEP', values: { rate: 0.05, depth: 8, feedback: 0.6, mix: 0.5 } },
      { name: 'WOBBLE', values: { rate: 5, depth: 3, feedback: 0.3, mix: 0.5 } },
    ],
  },
  {
    id: 'distortion', name: 'DISTORT', keepDry: false,
    params: [
      { key: 'drive', label: 'DRIVE', min: 0, max: 1, unit: '%' },
      { key: 'tone', label: 'TONE', min: 300, max: 12000, unit: 'Hz', log: true },
      { key: 'crush', label: 'CRUSH', min: 0, max: 1, unit: '%' },
      { key: 'mix', label: 'MIX', min: 0, max: 1, unit: '%' },
    ],
    presets: [
      { name: 'WARM', values: { drive: 0.25, tone: 5000, crush: 0, mix: 0.7 } },
      { name: 'CRUNCH', values: { drive: 0.55, tone: 3500, crush: 0, mix: 1 } },
      { name: 'FUZZ', values: { drive: 0.95, tone: 2200, crush: 0, mix: 1 } },
      { name: '8-BIT', values: { drive: 0.15, tone: 8000, crush: 0.8, mix: 1 } },
      { name: 'DESTROY', values: { drive: 1, tone: 1200, crush: 0.95, mix: 1 } },
    ],
  },
  {
    id: 'chorus', name: 'CHORUS', keepDry: false,
    params: [
      { key: 'rate', label: 'RATE', min: 0.1, max: 8, unit: 'hz', log: true },
      { key: 'depth', label: 'DEPTH', min: 0.5, max: 12, unit: 'ms' },
      { key: 'delay', label: 'DELAY', min: 5, max: 40, unit: 'ms' },
      { key: 'mix', label: 'MIX', min: 0, max: 1, unit: '%' },
    ],
    presets: [
      { name: 'SOFT', values: { rate: 0.8, depth: 2, delay: 20, mix: 0.4 } },
      { name: 'LUSH', values: { rate: 0.4, depth: 5, delay: 25, mix: 0.5 } },
      { name: 'VIBRATO', values: { rate: 5, depth: 3, delay: 8, mix: 1 } },
      { name: 'SEASICK', values: { rate: 0.15, depth: 12, delay: 30, mix: 0.7 } },
      { name: 'SHIMMER', values: { rate: 6, depth: 1, delay: 12, mix: 0.5 } },
    ],
  },
  {
    id: 'delay', name: 'DELAY', keepDry: true,
    params: [
      { key: 'time', label: 'TIME', min: 20, max: 1000, unit: 'ms' },
      { key: 'feedback', label: 'FEEDBACK', min: 0, max: 0.85, unit: '%' },
      { key: 'tone', label: 'TONE', min: 300, max: 12000, unit: 'Hz', log: true },
      { key: 'mix', label: 'MIX', min: 0, max: 1, unit: '%' },
    ],
    presets: [
      { name: 'NORMAL', mode: 'mono', values: { time: 350, feedback: 0.4, tone: 6000, mix: 0.4 } },
      { name: 'PING PONG', mode: 'pingpong', values: { time: 300, feedback: 0.5, tone: 5000, mix: 0.5 } },
      { name: 'SLAPBACK', mode: 'mono', values: { time: 90, feedback: 0.15, tone: 8000, mix: 0.45 } },
      { name: 'DUB', mode: 'pingpong', values: { time: 450, feedback: 0.6, tone: 1800, mix: 0.45 } },
      { name: 'CRAZY', mode: 'wobble', values: { time: 120, feedback: 0.65, tone: 4000, mix: 0.5 } },
    ],
  },
];

// Tabs are shown in signal-chain order so the UI matches what the audio does.
export const EFFECTS = CHAIN_ORDER.map(id => DEFINITIONS.find(d => d.id === id));

// Slider position (0..1) <-> parameter value.
export function toNorm(p, v) {
  return p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min);
}
export function fromNorm(p, t) {
  t = Math.max(0, Math.min(1, t));
  return p.log ? p.min * Math.pow(p.max / p.min, t) : p.min + (p.max - p.min) * t;
}
export function formatValue(p, v) {
  switch (p.unit) {
    case 's': return `${v.toFixed(2)} s`;
    case 'ms': return `${Math.round(v)} ms`;
    case 'hz': return `${v.toFixed(2)} Hz`;
    case 'Hz': return v >= 1000 ? `${(v / 1000).toFixed(1)} kHz` : `${Math.round(v)} Hz`;
    default: return `${Math.round(v * 100)}%`;
  }
}

// ---- Audio implementations --------------------------------------------------------------

function ramp(param, value, ctx) {
  param.setTargetAtTime(value, ctx.currentTime, 0.02);
}

class Effect {
  constructor(ctx, keepDry) {
    this.ctx = ctx;
    this.keepDry = keepDry;
    this.input = ctx.createGain();
    this.output = ctx.createGain();
    this.dry = ctx.createGain();
    this.wet = ctx.createGain();
    this.wet.gain.value = 0;
    this.input.connect(this.dry);
    this.dry.connect(this.output);
    this.wet.connect(this.output);
  }
  configure({ enabled, mode, values }) {
    const mix = enabled ? values.mix : 0;
    ramp(this.wet.gain, mix, this.ctx);
    ramp(this.dry.gain, this.keepDry ? 1 : 1 - mix, this.ctx);
    this.update(values, mode || 'normal', enabled);
  }
  update() {}
}

class Distortion extends Effect {
  constructor(ctx) {
    super(ctx, false);
    this.shaper = ctx.createWaveShaper();
    this.shaper.oversample = '2x';
    this.tone = ctx.createBiquadFilter();
    this.tone.type = 'lowpass';
    this.input.connect(this.shaper);
    this.shaper.connect(this.tone);
    this.tone.connect(this.wet);
    this.curveKey = '';
  }
  update(v) {
    const key = `${v.drive.toFixed(3)}|${v.crush.toFixed(3)}`;
    if (key !== this.curveKey) {
      this.curveKey = key;
      this.shaper.curve = makeDriveCurve(v.drive, v.crush);
    }
    ramp(this.tone.frequency, v.tone, this.ctx);
  }
}

// Soft clipping via tanh, then optional bit-depth reduction (stair-step curve).
function makeDriveCurve(drive, crush) {
  const n = 4096;
  const curve = new Float32Array(n);
  const gain = 1 + drive * 40;
  const bits = Math.round(16 - crush * 14);
  const levels = Math.pow(2, bits - 1);
  const norm = Math.tanh(gain);
  for (let i = 0; i < n; i++) {
    const x = (i / (n - 1)) * 2 - 1;
    let y = Math.tanh(x * gain) / norm;
    if (crush > 0) y = Math.round(y * levels) / levels;
    curve[i] = y * (1 - drive * 0.35); // tame the level increase from heavy drive
  }
  return curve;
}

class Chorus extends Effect {
  constructor(ctx) {
    super(ctx, false);
    this.delay = ctx.createDelay(0.2);
    this.delay.delayTime.value = 0.02;
    this.lfo = ctx.createOscillator();
    this.lfoGain = ctx.createGain();
    this.lfoGain.gain.value = 0;
    this.lfo.connect(this.lfoGain);
    this.lfoGain.connect(this.delay.delayTime);
    this.lfo.start();
    this.input.connect(this.delay);
    this.delay.connect(this.wet);
  }
  update(v) {
    const base = v.delay / 1000;
    const depth = Math.min(v.depth / 1000, base - 0.0005);
    ramp(this.lfo.frequency, v.rate, this.ctx);
    ramp(this.delay.delayTime, base, this.ctx);
    ramp(this.lfoGain.gain, depth, this.ctx);
  }
}

class Flanger extends Effect {
  constructor(ctx) {
    super(ctx, false);
    this.delay = ctx.createDelay(0.05);
    this.delay.delayTime.value = 0.0025;
    this.feedback = ctx.createGain();
    this.feedback.gain.value = 0; // GainNodes default to 1, which would make a runaway loop
    this.lfo = ctx.createOscillator();
    this.lfoGain = ctx.createGain();
    this.lfoGain.gain.value = 0;
    this.lfo.connect(this.lfoGain);
    this.lfoGain.connect(this.delay.delayTime);
    this.lfo.start();
    this.input.connect(this.delay);
    this.delay.connect(this.feedback);
    this.feedback.connect(this.delay);
    this.delay.connect(this.wet);
  }
  update(v, mode, enabled) {
    const depth = v.depth / 1000;
    ramp(this.lfo.frequency, v.rate, this.ctx);
    ramp(this.delay.delayTime, 0.0005 + depth, this.ctx);
    ramp(this.lfoGain.gain, depth, this.ctx);
    ramp(this.feedback.gain, enabled ? v.feedback : 0, this.ctx);
  }
}

// Two delay lines. Mono mode uses only the left line feeding itself. Ping-pong cross-feeds
// the two lines and pans them apart. Wobble is mono with an LFO bending the delay time.
class Delay extends Effect {
  constructor(ctx) {
    super(ctx, true);
    this.delayL = ctx.createDelay(2);
    this.delayR = ctx.createDelay(2);
    this.filterL = ctx.createBiquadFilter();
    this.filterR = ctx.createBiquadFilter();
    this.filterL.type = this.filterR.type = 'lowpass';
    this.fbLL = ctx.createGain(); // left -> left (mono feedback)
    this.fbLR = ctx.createGain(); // left -> right (ping-pong)
    this.fbRL = ctx.createGain(); // right -> left (ping-pong)
    // Feedback must start at 0 and delays at a sane time. With the defaults (gain 1, delay 0)
    // the loop gain is 2 and the signal explodes to infinity, then NaN, and silences everything.
    this.fbLL.gain.value = this.fbLR.gain.value = this.fbRL.gain.value = 0;
    this.delayL.delayTime.value = this.delayR.delayTime.value = 0.3;
    this.filterL.frequency.value = this.filterR.frequency.value = 6000;
    this.panL = ctx.createStereoPanner();
    this.panR = ctx.createStereoPanner();
    this.lfo = ctx.createOscillator();
    this.lfo.frequency.value = 0.7;
    this.lfoGain = ctx.createGain();
    this.lfoGain.gain.value = 0;
    this.lfo.connect(this.lfoGain);
    this.lfoGain.connect(this.delayL.delayTime);
    this.lfo.start();

    this.input.connect(this.delayL);
    this.delayL.connect(this.filterL);
    this.filterL.connect(this.panL);
    this.panL.connect(this.wet);
    this.filterL.connect(this.fbLL);
    this.fbLL.connect(this.delayL);
    this.filterL.connect(this.fbLR);
    this.fbLR.connect(this.delayR);
    this.delayR.connect(this.filterR);
    this.filterR.connect(this.panR);
    this.panR.connect(this.wet);
    this.filterR.connect(this.fbRL);
    this.fbRL.connect(this.delayL);
  }
  update(v, mode, enabled) {
    const ctx = this.ctx;
    const fb = enabled ? v.feedback : 0;
    const pingpong = mode === 'pingpong';
    ramp(this.delayL.delayTime, v.time / 1000, ctx);
    ramp(this.delayR.delayTime, v.time / 1000, ctx);
    ramp(this.filterL.frequency, v.tone, ctx);
    ramp(this.filterR.frequency, v.tone, ctx);
    ramp(this.fbLL.gain, pingpong ? 0 : fb, ctx);
    ramp(this.fbLR.gain, pingpong ? fb : 0, ctx);
    ramp(this.fbRL.gain, pingpong ? fb : 0, ctx);
    ramp(this.panL.pan, pingpong ? -0.8 : 0, ctx);
    ramp(this.panR.pan, pingpong ? 0.8 : 0, ctx);
    ramp(this.lfoGain.gain, mode === 'wobble' ? (v.time / 1000) * 0.3 : 0, ctx);
  }
}

// Convolution with a generated noise impulse response. Rebuilding the response is debounced
// so dragging the SIZE slider doesn't rebuild it on every step.
class Reverb extends Effect {
  constructor(ctx) {
    super(ctx, true);
    this.pre = ctx.createDelay(0.2);
    this.conv = ctx.createConvolver();
    this.damp = ctx.createBiquadFilter();
    this.damp.type = 'lowpass';
    this.damp.frequency.value = 6000;
    this.input.connect(this.pre);
    this.pre.connect(this.conv);
    this.conv.connect(this.damp);
    this.damp.connect(this.wet);
    this.irKey = '';
    this.irTimer = null;
  }
  update(v, mode) {
    ramp(this.pre.delayTime, v.pre / 1000, this.ctx);
    ramp(this.damp.frequency, v.damp, this.ctx);
    const key = `${v.size.toFixed(2)}|${mode}`;
    if (key !== this.irKey) {
      this.irKey = key;
      clearTimeout(this.irTimer);
      this.irTimer = setTimeout(() => { this.conv.buffer = makeImpulse(this.ctx, v.size, mode); }, 120);
    }
  }
}

function makeImpulse(ctx, seconds, mode) {
  const rate = ctx.sampleRate;
  const n = Math.floor(seconds * rate);
  const buf = ctx.createBuffer(2, n, rate);
  for (let ch = 0; ch < 2; ch++) {
    const d = buf.getChannelData(ch);
    for (let i = 0; i < n; i++) {
      const t = i / rate;
      const env = mode === 'reverse'
        ? Math.exp(-6.9 * (seconds - t) / seconds)
        : Math.exp(-6.9 * t / seconds);
      d[i] = (Math.random() * 2 - 1) * env;
    }
  }
  return buf;
}

export function createEffectChain(ctx) {
  const byId = {
    distortion: new Distortion(ctx),
    chorus: new Chorus(ctx),
    flanger: new Flanger(ctx),
    delay: new Delay(ctx),
    reverb: new Reverb(ctx),
  };
  for (let i = 0; i < CHAIN_ORDER.length - 1; i++) {
    byId[CHAIN_ORDER[i]].output.connect(byId[CHAIN_ORDER[i + 1]].input);
  }
  return { input: byId[CHAIN_ORDER[0]].input, output: byId[CHAIN_ORDER[CHAIN_ORDER.length - 1]].output, byId };
}
