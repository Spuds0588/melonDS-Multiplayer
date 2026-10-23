#!/usr/bin/env python3
"""Score a saved console screenshot with the route's own stage test.

`tools/scripts/tetris-4p.py`'s `open_setup_table` decides that the game's setup
table is on screen by two numbers read off the host's **top** screen: its mean
grey level (the lit pages sit at 100-123/255, the title's transition and its "A
communication error has occurred" page at 5-7) and how far its 64x48 thumbnail is
from the waiting page. A stage shot taken before that test existed can therefore
be re-scored today, which is the only way to tell a real table in the record from
a dark transition that was called one.

    tools/scripts/stage-score.py [--waiting PNG] [--console N] SHOT [SHOT...]

Each shot is a row of consoles left to right, 256x192 per console. The unit
scored is the **host** console (1, leftmost) by default, because the route's test
compares a console against *its own* waiting page: a guest's page is a different
page (it reads 126.9-127.0/255 while the host's waiting page reads 122.6), so
scoring a guest against the host's reference is not a measurement of the same
thing. Prints one line per file and exits 1 if any file reads as something other
than the setup table.
"""

import argparse
import sys

from PIL import Image

# Same thresholds as open_setup_table() in tetris-4p.py.
DARK = 40.0
LIT = 80.0
TOP = 115.0
OTHER_PAGE = 6.0


def page(im, console):
    left = console * 256
    top = im.crop((left, 0, left + 256, 192))
    px = list(top.getdata())
    return sum(px) / len(px), top.resize((64, 48), Image.BOX).tobytes()


def diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--waiting", default=None,
                    help="a shot whose host console shows the waiting page")
    ap.add_argument("--console", type=int, default=1, help="1-based console to score")
    ap.add_argument("shots", nargs="+")
    args = ap.parse_args()

    if args.waiting:
        waiting = page(Image.open(args.waiting).convert("L"), 0)[1]
        wname = args.waiting
    else:
        waiting = None
        wname = "-"

    bad = 0
    print(f"reference waiting page: {wname}")
    for path in args.shots:
        im = Image.open(path).convert("L")
        bright, thumb = page(im, args.console - 1)
        d = diff(thumb, waiting) if waiting else float("nan")
        if bright < DARK:
            verdict = "DARK (transition or communication error)"
        elif bright < LIT:
            verdict = "MID-GREY (cross-fade)"
        elif bright > TOP:
            verdict = "LIT above the band (waiting page or other)"
        elif waiting and d <= OTHER_PAGE:
            verdict = "WAITING page (party not in the room)"
        else:
            verdict = "SETUP TABLE"
        print(f"{path:58s} p{args.console}  grey {bright:6.1f}/255  d={d:5.1f}  {verdict}")
        if verdict != "SETUP TABLE":
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
