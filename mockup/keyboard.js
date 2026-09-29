// Keyboard model: the full key range, zoom level, scroll position, layout and hit-testing.
// Everything is in "native" bottom-screen pixels (320x240) so the same math ports to the 3DS.

export const LOW_MIDI = 36;   // C2
export const HIGH_MIDI = 96;  // C7
const BLACK_IN_OCTAVE = new Set([1, 3, 6, 8, 10]);
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

export function noteName(midi) {
  return NAMES[midi % 12] + (Math.floor(midi / 12) - 1);
}

export class KeyboardModel {
  constructor() {
    this.keys = [];
    let white = 0;
    for (let midi = LOW_MIDI; midi <= HIGH_MIDI; midi++) {
      const black = BLACK_IN_OCTAVE.has(midi % 12);
      // A black key "belongs" to the white key on its left.
      this.keys.push({ midi, black, whiteIndex: black ? white - 1 : white });
      if (!black) white++;
    }
    this.whiteCount = white;                      // 36 white keys for C2..C7
    this.zoomLevels = [5, 7, 10, 14, 20, 28, 36]; // visible white keys per level
    this.zoomIndex = 3;                           // 14 white keys = two octaves
    this.scroll = 7;                              // leftmost white key index (7 = C3)
  }

  get visibleWhite() { return this.zoomLevels[this.zoomIndex]; }
  get maxScroll() { return this.whiteCount - this.visibleWhite; }

  setScroll(s) {
    this.scroll = Math.max(0, Math.min(this.maxScroll, Math.round(s)));
  }

  // "+" zooms in (fewer, bigger keys). "-" zooms out (more, smaller keys). Keeps the centre stable.
  zoomIn() { this._setZoom(this.zoomIndex - 1); }
  zoomOut() { this._setZoom(this.zoomIndex + 1); }
  _setZoom(index) {
    const next = Math.max(0, Math.min(this.zoomLevels.length - 1, index));
    if (next === this.zoomIndex) return;
    const centre = this.scroll + this.visibleWhite / 2;
    this.zoomIndex = next;
    this.setScroll(centre - this.visibleWhite / 2);
  }

  // Midi numbers of the leftmost and rightmost fully visible white keys.
  visibleRange() {
    const whites = this.keys.filter(k => !k.black);
    return [whites[this.scroll].midi, whites[this.scroll + this.visibleWhite - 1].midi];
  }

  // Returns drawable key rectangles for the given area: white keys first, then black keys on top.
  layout(area) {
    const ww = area.w / this.visibleWhite;
    const out = [];
    for (const key of this.keys) {
      if (key.black) continue;
      const i = key.whiteIndex - this.scroll;
      if (i < 0 || i >= this.visibleWhite) continue;
      const x0 = Math.round(area.x + i * ww);
      const x1 = Math.round(area.x + (i + 1) * ww);
      out.push({ key, x: x0, y: area.y, w: x1 - x0, h: area.h });
    }
    const bw = Math.max(4, Math.round(ww * 0.6));
    const bh = Math.round(area.h * 0.6);
    for (const key of this.keys) {
      if (!key.black) continue;
      // The black key sits on the boundary between its white key and the next one.
      const boundary = key.whiteIndex + 1 - this.scroll;
      if (boundary < 0 || boundary > this.visibleWhite) continue;
      const cx = area.x + boundary * ww;
      out.push({ key, x: Math.round(cx - bw / 2), y: area.y, w: bw, h: bh });
    }
    return out;
  }

  // Returns the key under (px, py) or null. Black keys win because they are drawn on top.
  hit(area, px, py) {
    const rects = this.layout(area);
    for (let i = rects.length - 1; i >= 0; i--) {
      const r = rects[i];
      if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) return r.key;
    }
    return null;
  }
}
