/* The frame payload's decoder: the one place that knows how a pull is packed.
 *
 * The encoder is the Rust side's `frames_payload` (src-tauri/src/frames.rs), and
 * the layout is described there: a `u32` entry count, then per entry the seat,
 * which of its screens are present, a `u64` snapshot sequence number, and each
 * included screen as 256x192 RGB555 words.
 *
 * It lives in its own file because two readers need the same bytes decoded the
 * same way: the page's frame pump (`index.html`), and the control channel's
 * screen readers (`see`, `bands`, `shot`, `digest` in lib.rs). Both call
 * `window.__mdframes.decode`, so the format has one reader, not two.
 */
(() => {
  'use strict';

  // The DS's own screen, in the wire format's terms. Not the page's layout
  // constants: a payload is always one console's 256x192 screens.
  const PX = 256 * 192;

  // 15-bit RGB -> canvas RGBA words, built once. The DS has five bits per
  // channel, so this expansion is lossless: there is nothing to round away.
  const RGB555 = (() => {
    const table = new Uint32Array(32768);
    for (let v = 0; v < 32768; v++) {
      const r5 = v & 31, g5 = (v >> 5) & 31, b5 = (v >> 10) & 31;
      const r8 = (r5 << 3) | (r5 >> 2);
      const g8 = (g5 << 3) | (g5 >> 2);
      const b8 = (b5 << 3) | (b5 >> 2);
      // Canvas ImageData in memory is R,G,B,A - little-endian, so the word is
      // 0xAABBGGRR.
      table[v] = 0xFF000000 | (b8 << 16) | (g8 << 8) | r8;
    }
    return table;
  })();

  // One reusable RGBA buffer per seat: a 60 Hz pull that allocated 1.5 MB a
  // frame at four consoles would spend more time in the collector than in the
  // copy. Entries returned by `decode` alias these buffers and are valid only
  // until the next decode - and only for the screens their entry lists.
  const scratch = new Map();
  function scratchFor(seat) {
    let s = scratch.get(seat);
    if (!s) {
      s = { top: new Uint8ClampedArray(PX * 4), bottom: new Uint8ClampedArray(PX * 4) };
      scratch.set(seat, s);
    }
    return s;
  }

  function decode(buf) {
    const dv = new DataView(buf);
    const count = dv.getUint32(0, true);
    const out = [];
    let off = 4;
    for (let i = 0; i < count; i++) {
      const seat = dv.getUint32(off, true);
      const screens = dv.getUint32(off + 4, true);
      const seq = dv.getBigUint64(off + 8, true);
      off += 16;
      const s = scratchFor(seat);
      for (const [bit, dst] of [[1, s.top], [2, s.bottom]]) {
        if (!(screens & bit)) continue;
        const words = new Uint16Array(buf, off, PX);
        const view = new Uint32Array(dst.buffer);
        for (let p = 0; p < PX; p++) view[p] = RGB555[words[p]];
        off += PX * 2;
      }
      out.push({ seat, screens, seq, top: s.top, bottom: s.bottom });
    }
    return out;
  }

  // One name, because the control channel's readers call it too (see `__mdctl`
  // in lib.rs); the page's pump uses the same object.
  window.__mdframes = { decode };
})();
