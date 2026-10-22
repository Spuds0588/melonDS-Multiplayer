#!/usr/bin/env python3
"""Press the rule-confirm A on 2-4 consoles, proving every press landed.

The Tetris DS room swallows a press made within a second or two of a state
change (measured 2026-10-22: the host's A opens the setup table 2-3 s after a
guest's accept and does nothing at 1.5 s), and the 2026-10-22 passes read
swallowed presses as game behaviour twice. So this driver does not send a press
and hope: each attempt takes the console's bottom panel before and after, waits
for it to move, and re-sends until it does - with the attempt count printed, so a
press that needed three tries is visible as such. A press that never moves the
panel is reported as LOST, with both shots kept.

    tools/scripts/tetris-confirm.py --order 1,2,3     # host first
    tools/scripts/tetris-confirm.py --order 2,3,1     # guests first

It assumes the consoles are already in the room's setup table (drive the route
first with `tools/scripts/tetris-4p.py --players 3`). The panel a console is on
is compared as a 64x48 gray thumbnail of its own 256x192 bottom screen, the same
identity `tools/scripts/tetris-4p.py` uses and the same one the menu sweep needed:
the top panel animates by up to 5.8 mean gray between two reads of one screen,
the bottom by 0.0.

Exit status is 0 only when every press in the order is proven landed.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from appctl import DEFAULT_PORT, Channel  # noqa: E402

# How long to give a press to show up, and how many times to re-send it. The
# measured swallow window is ~1.5 s; 2.2 s of patience covers a panel that only
# redraws when the next poll arrives, and five attempts is far more than the
# three a working press has ever needed.
PATIENCE = 2.2
ATTEMPTS = 5
MOVED = 6.0


def thumb(ch, player, name):
    from PIL import Image

    out = ch.call(cmd="shot", name=name)
    im = Image.open(out["path"]).convert("L")
    left = (player - 1) * 256
    return im.crop((left, 192, left + 256, 384)).resize((64, 48), Image.BOX).tobytes()


def diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def confirm(ch, player):
    before = thumb(ch, player, f"confirm-before-p{player}")
    for attempt in range(1, ATTEMPTS + 1):
        ch.call(cmd="press", player=player, buttons=0x1, ms=200)
        start = time.time()
        while time.time() - start < PATIENCE:
            time.sleep(0.15)
            if diff(before, thumb(ch, player, f"confirm-p{player}")) > MOVED:
                print(f"  p{player}: press landed on attempt {attempt}", flush=True)
                return attempt
    print(f"  p{player}: press LOST after {ATTEMPTS} attempts - panel never moved", flush=True)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--order", default="1,2,3",
                    help="comma-separated console numbers, in the order they confirm")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = ap.parse_args()

    ch = Channel(args.port)
    st = ch.call(cmd="status")
    if not st.get("active"):
        raise SystemExit("no session running - start the app with tools/run_app.sh first")
    order = [int(x) for x in args.order.split(",")]
    print(f"confirm order {order}", flush=True)

    landed = {}
    for p in order:
        landed[p] = confirm(ch, p)
        time.sleep(0.5)

    for p in order:
        shot = ch.call(cmd="shot", name=f"confirm-after-p{p}")
        print(f"  p{p}: after -> {shot['path']}", flush=True)

    lost = [p for p, n in landed.items() if n is None]
    if lost:
        print(f"FAILED: p{lost} never took the press", flush=True)
        return 1
    print("every press proven landed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
