#!/usr/bin/env python3
"""Read a line of DS text from a screenshot, one glyph at a time.

The ASCII renderer in `appctl.py` shows a screen; this shows a *word*. Both are
needed: the renderer tells you where the text is, this tells you what it says,
because an 8px DS font printed as a luminance ramp is a guess and a glyph
printed as `#`/`.` blocks is a letter.

    tools/glyphs.py logs/shots/postvs.png 74 78 100 14 120          # dark ink
    tools/glyphs.py logs/shots/postvs.png 74 85 100 10 100 light    # bright ink

Arguments: PNG X Y W H THRESHOLD [light|dark]. The crop is the one `appctl.py
bands` points at. Glyphs are split on blank columns, so each block below the
header is one character - read them off, or compare a block against a known
screen's block for the same letter (the font is fixed, so this is reliable).

Written for MKDS's post-CUT-OFF screens, whose messages are bright-on-dark and
which is why the `light` mode exists. 2026-09-26.
"""

import sys

from PIL import Image


def main():
    if len(sys.argv) < 7:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    x, y, w, h, thr = (int(v) for v in sys.argv[2:7])
    light = len(sys.argv) > 7 and sys.argv[7].lower() in ("light", "bright", "invert")

    im = Image.open(path).convert("L").crop((x, y, x + w, y + h))
    px = im.load()

    def ink(c, r):
        v = px[c, r]
        return v > thr if light else v < thr

    cols = [any(ink(c, r) for r in range(h)) for c in range(w)]
    runs, start = [], None
    for c, v in enumerate(cols):
        if v and start is None:
            start = c
        elif not v and start is not None:
            runs.append([start, c - 1])
            start = None
    if start is not None:
        runs.append([start, w - 1])

    merged = []
    for r in runs:
        if merged and r[0] - merged[-1][1] <= 2:
            merged[-1][1] = r[1]  # a 1-2px gap is inside a glyph, not between two
        else:
            merged.append(r)

    print(f"{len(merged)} glyph(s), {'bright' if light else 'dark'} ink < {thr}, crop {x},{y} {w}x{h}")
    for i, (a, b) in enumerate(merged):
        print(f"--- glyph {i + 1} (x={a + x}..{b + x}, {b - a + 1}px)")
        for r in range(h):
            print("   " + "".join("#" if ink(c, r) else "." for c in range(a, b + 1)))


if __name__ == "__main__":
    main()
