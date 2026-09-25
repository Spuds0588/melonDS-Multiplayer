#!/usr/bin/env python3
"""Generates the Tauri icons from the melonDS logo.

src-tauri/icons/ is referenced by tauri.conf.json, and tauri-build validates
every icon it names before the Rust crate will compile — an empty icons folder
therefore stops `cargo build` at the build script, before any Rust code runs.
This derives all five files from res/melon512.png so the app icon stays the
project's own melonDS artwork. Re-run after changing the source art.

    python3 tools/make_icons.py
"""

import io
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "res" / "melon512.png"
OUT = ROOT / "src-tauri" / "icons"

PNG_SIZES = {"32x32.png": 32, "128x128.png": 128, "128x128@2x.png": 256}
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

# Modern ICNS entries are bare PNGs: type (4 bytes, big-endian OSType),
# total chunk length, then the image. ic07/ic08/ic09 cover 128/256/512 px,
# which is everything tauri.conf.json asks for.
ICNS_SIZES = [("ic07", 128), ("ic08", 256), ("ic09", 512)]


def resized(base: Image.Image, size: int) -> Image.Image:
    return base.resize((size, size), Image.LANCZOS)


def write_icns(base: Image.Image, path: Path) -> None:
    chunks = b""
    for ostype, size in ICNS_SIZES:
        buf = io.BytesIO()
        resized(base, size).save(buf, format="PNG")
        png = buf.getvalue()
        chunks += ostype.encode("ascii") + struct.pack(">I", len(png) + 8) + png
    path.write_bytes(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)


def main() -> int:
    if not SRC.exists():
        print(f"source art missing: {SRC}")
        return 1
    base = Image.open(SRC).convert("RGBA")
    OUT.mkdir(parents=True, exist_ok=True)

    for name, size in PNG_SIZES.items():
        resized(base, size).save(OUT / name)
    print(f"wrote {len(PNG_SIZES)} pngs")

    # PIL builds the multi-resolution ico itself from the sizes list.
    resized(base, 256).save(OUT / "icon.ico", sizes=[(s, s) for s in ICO_SIZES])
    print(f"wrote icon.ico ({len(ICO_SIZES)} resolutions)")

    write_icns(base, OUT / "icon.icns")
    print("wrote icon.icns")

    # Self-check: every file the config names must exist and open cleanly,
    # because tauri-build validates them in the same way.
    for name in [*PNG_SIZES, "icon.ico", "icon.icns"]:
        path = OUT / name
        with Image.open(path) as im:
            im.load()
            print(f"  ok {name}: {im.format} {im.size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
