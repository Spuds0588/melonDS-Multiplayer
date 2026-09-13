#!/usr/bin/env python3
"""
frames_to_html.py - turn md_capture output into a self-contained contact sheet.

md_capture writes raw frames and a manifest; this reads both, encodes each frame
as a PNG (stdlib zlib only, no dependencies), and builds a single HTML page with
the images inlined as data URIs so it renders from a file:// path with no server.

It also does the part a human eye is bad at: it samples the diagnostic ROM's
button blocks on the bottom screen and reports which buttons are lit. That turns
"the picture changed" into "exactly A and L lit on console 1 and nothing on the
others", which is the property the product actually depends on.

Usage:
    python3 frames_to_html.py captures/
"""

from __future__ import annotations

import base64
import struct
import sys
import zlib
from pathlib import Path

SCREEN_W, SCREEN_H = 256, 192
FRAME_H = SCREEN_H * 2            # top screen stacked above bottom screen

# --- Diagnostic ROM geometry (keep in step with tools/mkdiagrom/mkdiagrom.py) --
NUM_BUTTONS = 12
BUTTON_NAMES = [
    "A", "B", "Select", "Start", "Right", "Left",
    "Up", "Down", "R", "L", "X", "Y",
]
BLOCK_W, BLOCK_H = 24, 28
BLOCK_X0, BLOCK_Y0 = 12, 30
BLOCK_DX, BLOCK_DY = 40, 60
BLOCKS_PER_ROW = 6

# --- The checks the contact sheet reports -------------------------------------
#
# Which blocks count as lit is worked out per block position, by taking the
# colour that block has in the *majority* of that instance's captures. The ROM
# only ever draws a block in one of two colours - the idle grey or the player's
# own - and at most a handful of blocks are lit at once, so the majority is
# always the idle colour. Reading it out of the data rather than hardcoding it
# keeps this script independent of any colour-space assumptions, which is where
# the first version of it went wrong.

EXPECTED_PRESSED = {
    ("player1", "hold_a_l"): {"A", "L"},
    ("player2", "hold_a_l"): set(),
    ("player3", "hold_a_l"): set(),
    ("player1", "release"): set(),
    ("player2", "hold_up_r_x"): {"Up", "R", "X"},
}


def png_encode(width: int, height: int, rgba: bytes) -> bytes:
    """Minimal RGBA PNG. Filter type 0 on every scanline."""
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


class Frame:
    """One capture: the raw BGRA image plus helpers to inspect it."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        expected = SCREEN_W * FRAME_H * 4
        if len(self.data) != expected:
            raise ValueError(f"{path.name}: expected {expected} bytes, got {len(self.data)}")

    def pixel(self, x: int, y: int) -> tuple[int, int, int]:
        """RGB at (x, y) in stacked-image coordinates, converting BGRA to RGB."""
        off = (y * SCREEN_W + x) * 4
        b, g, r, _a = self.data[off:off + 4]
        return (r, g, b)

    def bottom_pixel(self, x: int, y: int) -> tuple[int, int, int]:
        return self.pixel(x, y + SCREEN_H)

    def distinct_colours(self, limit: int = 12) -> set[tuple[int, int, int]]:
        seen: set[tuple[int, int, int]] = set()
        for y in range(0, FRAME_H, 3):
            for x in range(0, SCREEN_W, 3):
                seen.add(self.pixel(x, y))
                if len(seen) > limit:
                    return seen
        return seen

    def block_rgb(self, index: int) -> tuple[int, int, int]:
        """Colour at the centre of one button block on the bottom screen.

        Sampling the centre is enough: the ROM fills each block with one flat
        colour, so the centre tells us whether that console sees the button held.
        """
        row, col = divmod(index, BLOCKS_PER_ROW)
        x = BLOCK_X0 + col * BLOCK_DX + BLOCK_W // 2
        y = BLOCK_Y0 + row * BLOCK_DY + BLOCK_H // 2
        return self.bottom_pixel(x, y)

    def lit_buttons(self, idle: list[tuple[int, int, int]]) -> set[str]:
        """Blocks drawn in a different colour from their idle colour."""
        return {BUTTON_NAMES[i] for i in range(NUM_BUTTONS)
                if self.block_rgb(i) != idle[i]}

    def to_png_base64(self) -> str:
        """Compose the two screens with a divider and encode as a data URI."""
        divider = bytes((0x20, 0x20, 0x28, 0xFF)) * SCREEN_W
        rows = []
        for y in range(FRAME_H):
            stride = y * SCREEN_W * 4
            row = self.data[stride:stride + SCREEN_W * 4]
            rgba = bytearray()
            for x in range(SCREEN_W):
                b, g, r, a = row[x * 4:x * 4 + 4]
                rgba += bytes((r, g, b, a if a else 0xFF))
            rows.append(bytes(rgba))
            if y == SCREEN_H - 1:
                rows.append(divider)

        png = png_encode(SCREEN_W, len(rows), b"".join(rows))
        return base64.b64encode(png).decode("ascii")


def idle_colours(frames: list[Frame]) -> list[tuple[int, int, int]]:
    """Per block position, the colour that block has in most of these captures."""
    idle = []
    for index in range(NUM_BUTTONS):
        counts: dict[tuple[int, int, int], int] = {}
        for frame in frames:
            rgb = frame.block_rgb(index)
            counts[rgb] = counts.get(rgb, 0) + 1
        idle.append(max(counts.items(), key=lambda kv: kv[1])[0])
    return idle


def main() -> int:
    outdir = Path(sys.argv[1] if len(sys.argv) > 1 else "captures")
    manifest = outdir / "manifest.tsv"
    if not manifest.exists():
        print(f"no manifest at {manifest}; run md_capture first")
        return 1

    entries = []
    for line in manifest.read_text().splitlines()[1:]:
        if not line.strip():
            continue
        index, filename, instance, host_frame, note = line.split("\t", 4)
        entries.append({
            "index": int(index),
            "file": filename,
            "instance": instance,
            "frame": int(host_frame),
            "note": note,
        })

    frames: dict[tuple[str, str], Frame] = {}
    checks: list[tuple[bool, str]] = []

    label_of: dict[str, str] = {}
    for entry in entries:
        frame = Frame(outdir / entry["file"])
        stem = Path(entry["file"]).name[len(f"{entry['index']:02d}_"):-len(".bgra")]
        entry["stem"] = stem
        entry["img"] = frame
        frames[(entry["instance"], stem)] = frame
        label_of[entry["file"]] = f"{entry['instance']}/{stem}"

    # Calibrate the idle colour per instance, then mark up every capture.
    for instance in sorted({e["instance"] for e in entries}):
        idle = idle_colours([e["img"] for e in entries if e["instance"] == instance])
        for entry in entries:
            if entry["instance"] != instance:
                continue
            entry["idle"] = idle
            entry["lit"] = sorted(entry["img"].lit_buttons(idle))
            entry["colours"] = len(entry["img"].distinct_colours())
            entry["png"] = entry["img"].to_png_base64()

    # ---- the checks a human would otherwise have to eyeball ------------------
    for entry in entries:
        checks.append((entry["colours"] >= 2,
                       f"{label_of[entry['file']]}: not a blank screen "
                       f"({entry['colours']}+ colours)"))

    idles = [("player1", "idle"), ("player2", "idle"), ("player3", "idle")]
    for i in range(len(idles)):
        for j in range(i + 1, len(idles)):
            a, b = idles[i], idles[j]
            if a in frames and b in frames:
                same = frames[a].data == frames[b].data
                checks.append((not same,
                               f"{a[0]} and {b[0]} render differently (identity reaches the screen)"))

    if ("player1", "idle") in frames and ("player1", "animated") in frames:
        checks.append((frames[("player1", "idle")].data != frames[("player1", "animated")].data,
                       "player1/ animated: the picture moves"))

    for key, expected in EXPECTED_PRESSED.items():
        frame = frames.get(key)
        if frame is None:
            continue
        got = set(frame.lit_buttons(idle_colours(
            [e["img"] for e in entries if e["instance"] == key[0]])))
        checks.append((got == expected,
                       f"{key[0]}/{key[1]}: lit blocks are {sorted(got) or 'none'} "
                       f"(expected {sorted(expected) or 'none'})"))

    if ("player1", "state_advanced") in frames and ("player1", "state_replayed") in frames:
        same = (frames[("player1", "state_advanced")].data
                == frames[("player1", "state_replayed")].data)
        checks.append((same, "savestate: replaying 40 frames after a load reproduces the "
                             "pre-save-advance picture exactly"))

    # ---- render --------------------------------------------------------------
    cards = []
    for entry in entries:
        badge = "".join(f'<span class="btn">{b}</span>' for b in entry["lit"]) or \
                '<span class="dim">no buttons held</span>'
        cards.append(f"""
      <figure class="card">
        <div class="meta">
          <strong>{entry['instance']}</strong>
          <span class="stem">{entry['stem']}</span>
          <span class="tag">host frame {entry['frame']}</span>
        </div>
        <img alt="{entry['instance']} {entry['stem']}" src="data:image/png;base64,{entry['png']}">
        <figcaption>
          <p class="note">{entry['note']}</p>
          <p class="sensors">blocks lit: {badge} &nbsp;|&nbsp; {entry['colours']}+ colours</p>
        </figcaption>
      </figure>""")

    passed = sum(1 for ok, _ in checks if ok)
    results = "\n".join(
        f'<li class="{"ok" if ok else "fail"}">{"PASS" if ok else "FAIL"} &mdash; {text}</li>'
        for ok, text in checks)

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>melonDS Multiplayer - visual capture</title>
<style>
  :root {{ color-scheme: dark; }}
  body {{ margin: 0; padding: 28px; background: #14161a; color: #e8e8ec;
          font: 14px/1.5 ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif; }}
  h1 {{ font-size: 20px; margin: 0 0 4px; }}
  .sub {{ color: #9aa0aa; margin-bottom: 22px; }}
  .summary {{ background: #1c1f26; border: 1px solid #2b2f38; border-radius: 10px;
              padding: 14px 18px; margin-bottom: 26px; }}
  .summary h2 {{ font-size: 15px; margin: 0 0 10px; }}
  ul {{ margin: 0; padding-left: 0; list-style: none; }}
  li {{ padding: 3px 0 3px 26px; position: relative; font-variant-numeric: tabular-nums; }}
  li::before {{ position: absolute; left: 0; font-weight: 700; }}
  li.ok::before {{ content: "\\2713"; color: #56d364; }}
  li.fail::before {{ content: "\\2717"; color: #f85149; }}
  li.fail {{ color: #ffb4ae; }}
  .grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(268px, 1fr));
           gap: 20px; }}
  .card {{ margin: 0; background: #1c1f26; border: 1px solid #2b2f38;
           border-radius: 10px; overflow: hidden; }}
  .meta {{ display: flex; align-items: baseline; gap: 8px; padding: 9px 12px;
           border-bottom: 1px solid #2b2f38; font-size: 13px; }}
  .stem {{ color: #9aa0aa; }}
  .tag {{ margin-left: auto; color: #6e7681; font-size: 11px; }}
  img {{ display: block; width: 100%; image-rendering: pixelated; background: #000; }}
  figcaption {{ padding: 9px 12px 12px; }}
  .note {{ margin: 0 0 6px; color: #c9cdd4; font-size: 12.5px; }}
  .sensors {{ margin: 0; color: #8b919b; font-size: 11.5px; }}
  .btn {{ display: inline-block; background: #2d4a7a; color: #cfe3ff; border-radius: 4px;
          padding: 0 5px; margin-right: 3px; font-weight: 600; }}
  .dim {{ color: #6e7681; font-style: italic; }}
</style>
</head>
<body>
  <h1>melonDS Multiplayer &mdash; visual capture</h1>
  <div class="sub">Each card is one emulated console: top screen above, bottom screen below,
    captured straight out of the software renderer through the host bridge.</div>

  <div class="summary">
    <h2>{passed}/{len(checks)} checks passed</h2>
    <ul>
{results}
    </ul>
  </div>

  <div class="grid">{''.join(cards)}
  </div>
</body>
</html>
"""

    sheet = outdir / "sheet.html"
    sheet.write_text(html)

    print(f"{passed}/{len(checks)} checks passed")
    for ok, text in checks:
        print(f"  {'PASS' if ok else 'FAIL'}  {text}")
    print(f"\nwrote {sheet}")
    return 0 if passed == len(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
