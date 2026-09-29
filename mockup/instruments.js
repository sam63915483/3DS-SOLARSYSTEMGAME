// Keyboard presets. 'sample' is the recorded sound (handled by sampler.js); the rest are
// synthesized voices built from oscillators so they never depend on a recording.
// Every voice returns { midi, stop(fadeSeconds?), bend(semitones) }.

export const INSTRUMENTS = [
  { id: 'sample',   name: 'YOUR SAMPLE',  desc: 'The sound you recorded, pitched across the keys' },
  { id: 'dream',    name: 'DREAM PAD',    desc: 'Slow, wide, detuned pad that blooms as you hold it' },
  { id: 'bells',    name: 'GLASS BELLS',  desc: 'Bright FM bell tones with a long shimmering tail' },
  { id: 'aurora',   name: 'AURORA',       desc: 'Breathy choir-like voices drifting through vowels' },
  { id: 'supersaw', name: 'SUPERSAW',     desc: 'Seven detuned saws, punchy EDM lead' },
  { id: 'wobble',   name: 'WOBBLE BASS',  desc: 'Growling filter-wobble bass, an octave down' },
];

export function midiToFreq(midi) {
  return 440 * Math.pow(2, (midi - 69) / 12);
}

export function createSynthVoice(ctx, id, midi, dest) {
  const freq = midiToFreq(midi);
  switch (id) {
    case 'dream': return dreamPad(ctx, freq, midi, dest);
    case 'bells': return glassBells(ctx, freq, midi, dest);
    case 'aurora': return aurora(ctx, freq, midi, dest);
    case 'supersaw': return supersaw(ctx, freq, midi, dest);
    case 'wobble': return wobbleBass(ctx, freq, midi, dest);
    default: throw new Error('unknown instrument ' + id);
  }
}

// ---- Shared building blocks ------------------------------------------------------------

// Tracks oscillators so pitch bend can move them all, and handles the amp envelope + cleanup.
class VoiceBuilder {
  constructor(ctx, midi, dest, { attack, release, peak }) {
    this.ctx = ctx;
    this.midi = midi;
    this.release = release;
    this.oscs = []; // { osc, baseDetune }
    this.extras = []; // LFOs and other sources to stop
    this.amp = ctx.createGain();
    this.amp.gain.value = 0;
    this.amp.connect(dest);
    const t = ctx.currentTime;
    this.amp.gain.setValueAtTime(0, t);
    this.amp.gain.linearRampToValueAtTime(peak, t + attack);
    this.stopped = false;
  }
  osc(type, freq, detune = 0) {
    const o = this.ctx.createOscillator();
    o.type = type;
    o.frequency.value = freq;
    o.detune.value = detune;
    o.start();
    this.oscs.push({ osc: o, baseDetune: detune });
    return o;
  }
  lfo(type, rate, amount, target) {
    const o = this.ctx.createOscillator();
    o.type = type;
    o.frequency.value = rate;
    const g = this.ctx.createGain();
    g.gain.value = amount;
    o.connect(g);
    g.connect(target);
    o.start();
    this.extras.push(o);
    return o;
  }
  gain(value) {
    const g = this.ctx.createGain();
    g.gain.value = value;
    return g;
  }
  filter(type, freq, q = 1) {
    const f = this.ctx.createBiquadFilter();
    f.type = type;
    f.frequency.value = freq;
    f.Q.value = q;
    return f;
  }
  voice() {
    return {
      midi: this.midi,
      bend: (semi) => { for (const { osc, baseDetune } of this.oscs) osc.detune.value = baseDetune + semi * 100; },
      stop: (fade) => {
        if (this.stopped) return;
        this.stopped = true;
        const rel = fade === undefined ? this.release : Math.min(fade, this.release);
        const t = this.ctx.currentTime;
        this.amp.gain.cancelScheduledValues(t);
        this.amp.gain.setValueAtTime(this.amp.gain.value, t);
        this.amp.gain.setTargetAtTime(0, t, Math.max(0.005, rel / 4));
        const end = t + rel + 0.1;
        for (const { osc } of this.oscs) osc.stop(end);
        for (const o of this.extras) o.stop(end);
        setTimeout(() => this.amp.disconnect(), (rel + 0.2) * 1000);
      },
    };
  }
}

// ---- Instruments ---------------------------------------------------------------------------

// Ethereal 1: detuned saws + triangle + sub, filter opens slowly, gentle vibrato.
function dreamPad(ctx, freq, midi, dest) {
  const v = new VoiceBuilder(ctx, midi, dest, { attack: 0.5, release: 1.6, peak: 0.45 });
  const mix = v.gain(0.22);
  v.osc('sawtooth', freq, -9).connect(mix);
  v.osc('sawtooth', freq, 9).connect(mix);
  v.osc('triangle', freq, 0).connect(mix);
  const sub = v.gain(0.5);
  v.osc('sine', freq / 2).connect(sub);
  sub.connect(mix);
  const lp = v.filter('lowpass', 400, 1.2);
  const t = ctx.currentTime;
  lp.frequency.setValueAtTime(400, t);
  lp.frequency.exponentialRampToValueAtTime(2400, t + 0.8);
  mix.connect(lp);
  lp.connect(v.amp);
  for (const { osc } of v.oscs.slice(0, 3)) v.lfo('sine', 0.35, 6, osc.detune);
  return v.voice();
}

// Ethereal 2: FM bell. Modulator index decays quickly, leaving a pure ringing tone.
function glassBells(ctx, freq, midi, dest) {
  const v = new VoiceBuilder(ctx, midi, dest, { attack: 0.005, release: 1.8, peak: 0.5 });
  const carrier = v.osc('sine', freq);
  const mod = v.osc('sine', freq * 3.5);
  const index = v.gain(freq * 2.5);
  mod.connect(index);
  index.connect(carrier.frequency);
  const t = ctx.currentTime;
  index.gain.setValueAtTime(freq * 2.5, t);
  index.gain.setTargetAtTime(freq * 0.15, t, 0.35);
  const shimmer = v.gain(0.18);
  v.osc('sine', freq * 2, 6).connect(shimmer);
  carrier.connect(v.amp);
  shimmer.connect(v.amp);
  // Bells decay while held, down to a quiet sustain.
  v.amp.gain.setTargetAtTime(0.12, t + 0.01, 1.2);
  return v.voice();
}

// Ethereal 3: two triangles through vowel-like band-pass filters that drift with an LFO.
function aurora(ctx, freq, midi, dest) {
  const v = new VoiceBuilder(ctx, midi, dest, { attack: 0.9, release: 2.2, peak: 0.9 });
  const src = v.gain(0.4);
  v.osc('triangle', freq, -5).connect(src);
  v.osc('triangle', freq, 5).connect(src);
  const breath = v.gain(0.12);
  v.osc('sawtooth', freq, 0).connect(breath);
  breath.connect(src);
  const f1 = v.filter('bandpass', 700, 6);
  const f2 = v.filter('bandpass', 1200, 6);
  const body = v.filter('lowpass', 900, 0.7);
  src.connect(f1); src.connect(f2); src.connect(body);
  const sum = v.gain(1);
  f1.connect(sum); f2.connect(sum);
  const bodyGain = v.gain(0.35);
  body.connect(bodyGain); bodyGain.connect(sum);
  sum.connect(v.amp);
  v.lfo('sine', 0.18, 320, f1.frequency);
  v.lfo('sine', 0.23, 420, f2.frequency);
  for (const { osc } of v.oscs.slice(0, 2)) v.lfo('sine', 5, 4, osc.detune);
  return v.voice();
}

// EDM lead: seven detuned saws, quick pitch drop on attack, resonant low-pass.
function supersaw(ctx, freq, midi, dest) {
  const v = new VoiceBuilder(ctx, midi, dest, { attack: 0.01, release: 0.18, peak: 0.42 });
  const mix = v.gain(0.13);
  const t = ctx.currentTime;
  for (const d of [-36, -24, -12, 0, 12, 24, 36]) {
    const o = v.osc('sawtooth', freq, d);
    o.detune.setValueAtTime(d + 250, t);
    o.detune.setTargetAtTime(d, t, 0.03);
    o.connect(mix);
  }
  const sub = v.gain(0.2);
  v.osc('square', freq / 2).connect(sub);
  sub.connect(mix);
  const lp1 = v.filter('lowpass', 7000, 1.5);
  const lp2 = v.filter('lowpass', 7000, 0.7);
  lp1.frequency.setValueAtTime(7000, t);
  lp1.frequency.exponentialRampToValueAtTime(3000, t + 0.25);
  mix.connect(lp1); lp1.connect(lp2); lp2.connect(v.amp);
  return v.voice();
}

// Techno / dubstep bass: an octave down, resonant filter wobbled by a tempo-like LFO, then drive.
function wobbleBass(ctx, freq, midi, dest) {
  const v = new VoiceBuilder(ctx, midi, dest, { attack: 0.01, release: 0.12, peak: 0.45 });
  const f = freq / 2;
  const mix = v.gain(0.3);
  v.osc('sawtooth', f).connect(mix);
  v.osc('square', f, 7).connect(mix);
  const sub = v.gain(0.5);
  v.osc('sine', f / 2).connect(sub);
  sub.connect(mix);
  const lp = v.filter('lowpass', 900, 9);
  v.lfo('triangle', 4.67, 700, lp.frequency); // ~1/8 notes at 140 BPM
  const drive = ctx.createWaveShaper();
  const curve = new Float32Array(1024);
  for (let i = 0; i < 1024; i++) { const x = (i / 1023) * 2 - 1; curve[i] = Math.tanh(x * 3); }
  drive.curve = curve;
  mix.connect(lp); lp.connect(drive); drive.connect(v.amp);
  return v.voice();
}
