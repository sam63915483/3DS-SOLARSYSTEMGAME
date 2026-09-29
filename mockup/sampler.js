// Audio engine: one sample, one voice, and a mic recorder capped at MAX_SECONDS.
// Pitch is changed by playback rate, which is exactly what the 3DS DSP (ndsp) can do per channel.

import { createEffectChain } from './effects.js';
import { createSynthVoice } from './instruments.js';

export const MAX_SECONDS = 10;
export const ROOT_MIDI = 60;      // C4 plays the sample at its recorded pitch
export const WAVE_COLUMNS = 368;  // waveform resolution on the top screen

export class Sampler {
  constructor() {
    this.ctx = null;
    this.sample = null;        // AudioBuffer
    this.sampleIsBuiltIn = true;
    this.voice = null;         // { src, gain, midi }
    this.rec = null;           // active recording session
    this.error = null;
    this.peakCache = null;
    this.trimmedMs = 0;        // leading silence removed from the last recording
    this.loopStart = 0;        // seconds; where playback jumps back to after the crossfaded tail
    this.master = null;        // voices -> master -> pad filters -> effects -> analyser -> speakers
    this.padLow = null;        // circle pad left: low-pass sweep
    this.padHigh = null;       // circle pad right: high-pass sweep
    this.bend = 0;             // circle pad up/down: pitch bend in semitones
    this.instrument = 'sample';
    this.chain = null;
    this.fxConfigs = {};       // last config per effect id, applied once the chain exists
    this.analyser = null;
    this._wave = null;
    this._freq = null;
  }

  // Must be called from a user gesture the first time (browser autoplay rules).
  ensure() {
    if (!this.ctx) {
      const ctx = new (window.AudioContext || window.webkitAudioContext)();
      this.ctx = ctx;
      this.master = ctx.createGain();
      this.analyser = ctx.createAnalyser();
      this.analyser.fftSize = 2048;
      this.analyser.smoothingTimeConstant = 0.7;
      this.padLow = ctx.createBiquadFilter();
      this.padLow.type = 'lowpass';
      this.padLow.frequency.value = 20000;
      this.padLow.Q.value = 2;
      this.padHigh = ctx.createBiquadFilter();
      this.padHigh.type = 'highpass';
      this.padHigh.frequency.value = 10;
      this.padHigh.Q.value = 2;
      this.master.connect(this.padLow);
      this.padLow.connect(this.padHigh);
      this.analyser.connect(ctx.destination);
      this.buildChain();
      this._wave = new Float32Array(this.analyser.fftSize);
      this._freq = new Uint8Array(this.analyser.frequencyBinCount);
      this.setSample(makeBuiltInSample(ctx.sampleRate), ctx.sampleRate, true);
    }
    if (this.ctx.state === 'suspended') this.ctx.resume();
  }

  // Installs raw PCM as the instrument, with the loop seam crossfaded so it doesn't click.
  setSample(data, rate, builtIn) {
    const looped = makeLoopable(data, rate);
    const buf = this.ctx.createBuffer(1, looped.data.length, rate);
    buf.getChannelData(0).set(looped.data);
    this.sample = buf;
    this.loopStart = looped.loopStart / rate;
    this.sampleIsBuiltIn = builtIn;
    this.peakCache = null;
  }

  // (Re)creates the effect chain between the pad filters and the analyser, restoring settings.
  buildChain() {
    if (this.chain) {
      this.padHigh.disconnect(this.chain.input);
      this.chain.output.disconnect(this.analyser);
    }
    this.chain = createEffectChain(this.ctx);
    this.padHigh.connect(this.chain.input);
    this.chain.output.connect(this.analyser);
    for (const id of Object.keys(this.fxConfigs)) this.chain.byId[id].configure(this.fxConfigs[id]);
  }

  // Pushes an effect's settings to the audio graph (or stores them until the graph exists).
  applyFx(id, config) {
    this.fxConfigs[id] = config;
    if (this.chain) this.chain.byId[id].configure(config);
  }

  // Circle pad: x, y in -1..1. Up bends pitch up an octave, down bends down an octave.
  // Left sweeps a low-pass filter closed, right sweeps a high-pass filter up.
  setPad(x, y) {
    this.bend = y * 12;
    if (this.voice) this.voice.bend(this.bend);
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    const low = x < 0 ? 20000 * Math.pow(250 / 20000, -x) : 20000;
    const high = x > 0 ? 10 * Math.pow(5000 / 10, x) : 10;
    this.padLow.frequency.setTargetAtTime(low, t, 0.01);
    this.padHigh.frequency.setTargetAtTime(high, t, 0.01);
  }

  // Snapshot of what the speakers are playing right now, for the visualizer. Null before audio starts.
  scope() {
    if (!this.analyser) return null;
    this.analyser.getFloatTimeDomainData(this._wave);
    this.analyser.getByteFrequencyData(this._freq);
    let sumSq = 0;
    for (let i = 0; i < this._wave.length; i++) sumSq += this._wave[i] * this._wave[i];
    if (!Number.isFinite(sumSq)) {
      // A feedback loop went to infinity/NaN and would stay silent forever. Rebuild it.
      this.buildChain();
      this._wave.fill(0);
      this._freq.fill(0);
      sumSq = 0;
    }
    return {
      wave: this._wave,
      freq: this._freq,
      rms: Math.sqrt(sumSq / this._wave.length),
      midi: this.voice ? this.voice.midi : null,
    };
  }

  get isRecording() { return this.rec !== null; }
  get recordedSeconds() { return this.rec ? this.rec.total / this.ctx.sampleRate : 0; }
  get inputLevel() { return this.rec ? this.rec.level : 0; }
  get sampleSeconds() { return this.sample ? this.sample.duration : 0; }

  setInstrument(id) {
    this.instrument = id;
  }

  noteOn(midi) {
    this.ensure();
    // Retrigger: cut the old voice quickly (synth voices clamp this to their own release).
    if (this.voice) { this.voice.stop(0.03); this.voice = null; }
    const voice = this.instrument === 'sample'
      ? this.sampleVoice(midi)
      : createSynthVoice(this.ctx, this.instrument, midi, this.master);
    voice.bend(this.bend);
    this.voice = voice;
  }

  noteOff() {
    if (!this.voice) return;
    this.voice.stop();
    this.voice = null;
  }

  // The recorded sound, looped and pitched by playback rate.
  sampleVoice(midi) {
    const ctx = this.ctx;
    const t = ctx.currentTime;
    const src = ctx.createBufferSource();
    src.buffer = this.sample;
    src.loop = true; // keeps repeating until the key is released
    src.loopStart = this.loopStart;
    src.loopEnd = this.sample.duration;
    const gain = ctx.createGain();
    gain.gain.setValueAtTime(0, t);
    gain.gain.linearRampToValueAtTime(1, t + 0.004);
    src.connect(gain);
    gain.connect(this.master);
    src.start(t);
    let stopped = false;
    return {
      midi,
      bend: (semi) => { src.playbackRate.value = Math.pow(2, (midi - ROOT_MIDI + semi) / 12); },
      stop: (fade = 0.04) => {
        if (stopped) return;
        stopped = true;
        const now = ctx.currentTime;
        gain.gain.cancelScheduledValues(now);
        gain.gain.setValueAtTime(gain.gain.value, now);
        gain.gain.linearRampToValueAtTime(0, now + fade);
        src.stop(now + fade + 0.01);
      },
    };
  }

  async startRecording() {
    this.ensure();
    if (this.rec) return;
    this.error = null;
    // Silence the speakers while the mic is open so playback and effect tails don't end up
    // in the recording: stop the voice, mute the output, and drop any delay/reverb tails.
    this.noteOff();
    this.master.gain.setTargetAtTime(0, this.ctx.currentTime, 0.01);
    this.buildChain();
    let stream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false },
      });
    } catch (err) {
      this.error = 'MIC UNAVAILABLE: ' + err.message;
      this.master.gain.setTargetAtTime(1, this.ctx.currentTime, 0.02);
      return;
    }
    if (this.rec) { stream.getTracks().forEach(t => t.stop()); return; }

    const ctx = this.ctx;
    const maxSamples = Math.floor(MAX_SECONDS * ctx.sampleRate);
    const source = ctx.createMediaStreamSource(stream);
    const sink = ctx.createGain();
    sink.gain.value = 0; // the recorder must reach the destination to run, but the mic must stay silent
    const rec = {
      stream, source, sink, node: null,
      chunks: [], total: 0, maxSamples, level: 0,
      discard: Math.round(ctx.sampleRate * DISCARD_MS / 1000),
      colsMin: new Float32Array(WAVE_COLUMNS),
      colsMax: new Float32Array(WAVE_COLUMNS),
    };

    // Capture runs on the audio thread (AudioWorklet) so a busy main thread can't drop
    // samples and leave clicks in the recording. ScriptProcessor is the fallback.
    if (ctx.audioWorklet) {
      try {
        await loadRecorderWorklet(ctx);
        const node = new AudioWorkletNode(ctx, 'sk1-recorder', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1] });
        node.port.onmessage = (e) => { if (this.rec === rec) this._ingest(rec, e.data); };
        rec.node = node;
      } catch (err) {
        rec.node = null;
      }
    }
    if (!rec.node) {
      const proc = ctx.createScriptProcessor(2048, 1, 1);
      proc.onaudioprocess = (e) => { if (this.rec === rec) this._ingest(rec, e.inputBuffer.getChannelData(0)); };
      rec.node = proc;
    }
    if (this.rec) { stream.getTracks().forEach(t => t.stop()); return; }
    source.connect(rec.node);
    rec.node.connect(sink);
    sink.connect(ctx.destination);
    this.rec = rec;
  }

  // Appends a block of mic samples to the recording and updates the live waveform/level.
  _ingest(rec, input) {
    if (rec.discard > 0) {   // the speaker is still fading out: don't let it into the sample
      const skip = Math.min(rec.discard, input.length);
      rec.discard -= skip;
      input = input.subarray(skip);
      if (input.length === 0) return;
    }
    const take = Math.min(input.length, rec.maxSamples - rec.total);
    if (take <= 0) return;
    const chunk = input.slice(0, take);
    const perCol = rec.maxSamples / WAVE_COLUMNS;
    let sumSq = 0;
    for (let i = 0; i < take; i++) {
      const s = chunk[i];
      sumSq += s * s;
      const c = Math.min(WAVE_COLUMNS - 1, Math.floor((rec.total + i) / perCol));
      if (s < rec.colsMin[c]) rec.colsMin[c] = s;
      if (s > rec.colsMax[c]) rec.colsMax[c] = s;
    }
    rec.chunks.push(chunk);
    rec.total += take;
    rec.level = Math.sqrt(sumSq / take);
    if (rec.total >= rec.maxSamples) this.stopRecording();
  }

  stopRecording() {
    const rec = this.rec;
    if (!rec) return;
    this.rec = null;
    if (rec.node.port) { rec.node.port.postMessage('stop'); rec.node.port.onmessage = null; }
    else rec.node.onaudioprocess = null;
    rec.source.disconnect();
    rec.node.disconnect();
    rec.sink.disconnect();
    rec.stream.getTracks().forEach(t => t.stop());
    this.master.gain.setTargetAtTime(1, this.ctx.currentTime, 0.02);
    if (rec.total < 1) return;
    const raw = new Float32Array(rec.total);
    let offset = 0;
    for (const chunk of rec.chunks) { raw.set(chunk, offset); offset += chunk.length; }

    // Trim silence so the sample starts the instant the sound does. Without this the mic
    // start-up gap plus reaction time becomes a dead pause on every key press.
    const rate = this.ctx.sampleRate;
    const [start, end] = trimRange(raw, rate);
    this.trimmedMs = Math.round(1000 * start / rate);
    this.setSample(normalize(raw.slice(start, end)), rate, false);
    this.instrument = 'sample'; // a fresh recording always becomes the active keyboard sound
  }

  // Waveform of the recording in progress, laid out over the full 10 s width.
  livePeaks() {
    return this.rec ? { min: this.rec.colsMin, max: this.rec.colsMax } : null;
  }

  // Waveform of the current sample, one [min,max] pair per column.
  peaks() {
    if (!this.sample) return null;
    if (!this.peakCache) {
      const data = this.sample.getChannelData(0);
      const min = new Float32Array(WAVE_COLUMNS);
      const max = new Float32Array(WAVE_COLUMNS);
      const perCol = data.length / WAVE_COLUMNS;
      for (let c = 0; c < WAVE_COLUMNS; c++) {
        const start = Math.floor(c * perCol);
        const end = Math.min(data.length, Math.floor((c + 1) * perCol));
        let lo = 0, hi = 0;
        for (let i = start; i < end; i++) {
          if (data[i] < lo) lo = data[i];
          if (data[i] > hi) hi = data[i];
        }
        min[c] = lo; max[c] = hi;
      }
      this.peakCache = { min, max };
    }
    return this.peakCache;
  }
}

// Trimming works on a 10 ms RMS envelope. The sound starts where the envelope first reaches
// a level relative to the loudest moment (with an absolute floor so faint noise never counts)
// and ends where it last stays above a lower "decay" level.
const ABS_FLOOR = 0.04;       // RMS below this is always silence (about -28 dBFS)
const START_REL = 0.15;       // start: within -16 dB of the loudest window
const END_REL = 0.03;         // end: last window within -30 dB of the loudest
const PRE_ROLL_MS = 10;       // keep a little audio before the start so attacks stay intact
const TAIL_MS = 60;           // keep a short tail after the sound decays
const NORMALIZE_PEAK = 0.9;
const MAX_GAIN = 20;
const DISCARD_MS = 200;       // mic input to throw away right after REC (speaker still fading)

// Returns [start, end) sample indices around the recorded sound. Whole range if all quiet.
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

// Scales the recording so its peak sits at NORMALIZE_PEAK (quiet recordings play at full level).
export function normalize(data) {
  let peak = 0;
  for (let i = 0; i < data.length; i++) { const a = Math.abs(data[i]); if (a > peak) peak = a; }
  if (peak < 1e-4) return data;
  const gain = Math.min(MAX_GAIN, NORMALIZE_PEAK / peak);
  for (let i = 0; i < data.length; i++) data[i] *= gain;
  return data;
}

// The recorder processor: batches 128-frame render quanta into 1024-sample blocks and posts
// them to the main thread. Runs on the audio thread, so it never misses a block.
const RECORDER_WORKLET = `
class Sk1Recorder extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buf = new Float32Array(1024);
    this.n = 0;
    this.stopped = false;
    this.port.onmessage = () => { this.stopped = true; };
  }
  process(inputs) {
    if (this.stopped) return false;
    const ch = inputs[0] && inputs[0][0];
    if (!ch) return true;
    let i = 0;
    while (i < ch.length) {
      const take = Math.min(ch.length - i, this.buf.length - this.n);
      this.buf.set(ch.subarray(i, i + take), this.n);
      this.n += take;
      i += take;
      if (this.n === this.buf.length) {
        this.port.postMessage(this.buf, [this.buf.buffer]);
        this.buf = new Float32Array(1024);
        this.n = 0;
      }
    }
    return true;
  }
}
registerProcessor('sk1-recorder', Sk1Recorder);
`;
let recorderWorkletLoaded = null;
function loadRecorderWorklet(ctx) {
  if (!recorderWorkletLoaded) {
    const url = URL.createObjectURL(new Blob([RECORDER_WORKLET], { type: 'application/javascript' }));
    recorderWorkletLoaded = ctx.audioWorklet.addModule(url);
  }
  return recorderWorkletLoaded;
}

const LOOP_FADE_MS = 50;
const START_FADE_MS = 2;

// Crossfades the tail of the sample into its head so looping has no audible seam.
// Playback runs 0..end once, then loops from `loopStart` (the head was already blended into the tail).
export function makeLoopable(data, rate) {
  const out = Float32Array.from(data);
  // Tiny fade-in so the very first play never starts with a click.
  const startFade = Math.min(Math.round(rate * START_FADE_MS / 1000), Math.floor(data.length / 8));
  for (let i = 0; i < startFade; i++) out[i] = data[i] * (i / startFade);
  const fade = Math.min(Math.round(rate * LOOP_FADE_MS / 1000), Math.floor(data.length / 4));
  const tailStart = data.length - fade;
  for (let i = 0; i < fade; i++) {
    const t = i / fade;
    out[tailStart + i] = data[tailStart + i] * Math.cos(t * Math.PI / 2) + data[i] * Math.sin(t * Math.PI / 2);
  }
  return { data: out, loopStart: fade };
}

// A plucky one-second tone at middle C so the keyboard is playable before anything is recorded.
function makeBuiltInSample(rate) {
  const seconds = 1.0;
  const data = new Float32Array(Math.floor(seconds * rate));
  const f = 261.63;
  for (let i = 0; i < data.length; i++) {
    const t = i / rate;
    const env = Math.min(1, t / 0.005) * Math.exp(-4 * t);
    data[i] = env * 0.5 * (
      Math.sin(2 * Math.PI * f * t) +
      0.5 * Math.sin(2 * Math.PI * 2 * f * t) +
      0.25 * Math.sin(2 * Math.PI * 3 * f * t)
    );
  }
  return data;
}
