#!/usr/bin/env python3
"""Drive open_setup_table() with a scripted console, no emulator.

The gate in tetris-4p.py is what decides that a run "reached the setup table",
so it is the part of the route that can lie. This exercises it against the grey
levels and page movements measured on 2026-10-23 (table 100.7-101, waiting page
122.6, cross-fade 51, blank/error 4.8-7) and asserts the four outcomes:

  * a fade at 51 is not a stage, the table at 101 is, and that takes two samples;
  * a waiting page whose player list changed reads 122 so it is not a stage;
  * a dark page aborts and the abort leaves a `setup-failed` shot behind;
  * a press that moves nothing is sent once more.

    tools/scripts/stage-gate-test.py
"""

import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("tetris4p", os.path.join(HERE, "tetris-4p.py"))
route = importlib.util.module_from_spec(spec)
spec.loader.exec_module(route)

WAITING = bytes([0] * 64)
TABLE = bytes([120] * 64)
FADE = bytes([60] * 64)
LIST = bytes([40] * 64)   # the waiting page after a player joins: d = 10


class Scripted:
    """A console that shows `pages` in order, repeating the last one for ever."""

    p = 1

    def __init__(self, pages, moves=True):
        self.pages = list(pages)
        self.moves = moves
        self.shots = []
        self.presses = 0
        self.sigvalue = bytes([1] * 32)

    def top_page(self):
        bright, page = self.pages[0] if len(self.pages) == 1 else self.pages.pop(0)
        return bright, page

    diff = staticmethod(route.Pad.diff)

    def sig(self):
        return self.sigvalue

    def press(self, mask):
        self.presses += 1
        if self.moves:
            self.sigvalue = bytes([(self.sigvalue[0] + 40) % 256]) * 32

    def shot(self, stage):
        self.shots.append(stage)


def run(name, pages, moves=True, settle=1.0, expect=None, presses=2):
    host = Scripted(pages, moves)
    try:
        got = ("table", route.open_setup_table(host, presses=presses, settle=settle))
    except SystemExit as e:
        got = ("abort", str(e))
    print(f"{name}: {got[0]}", flush=True)
    if expect is not None:
        assert got[0] == expect, f"{name}: expected {expect}, got {got[0]}"
    return host, got


def main():
    # Each script starts with the waiting page, because that is what the gate
    # snapshots as the reference before it presses.
    # 1. fade first, then the table: must return the table, on the second sample.
    host, (kind, attempt) = run(
        "fade then table", [(122, WAITING), (51, FADE), (101, TABLE)], expect="table")
    assert host.presses == 1, host.presses
    assert len(host.shots) == 0, host.shots

    # 2. a single table sample is not enough (a fade can share its grey).
    host, got = run("one sample only", [(122, WAITING), (101, TABLE), (5, FADE)],
                    settle=0.5, expect="abort")
    assert "never opened" in got[1], got[1]
    assert host.shots == ["setup-failed"], host.shots

    # 3. the waiting page with the player list filled in is lit above the band.
    host, got = run("waiting page, list changed", [(122, WAITING), (122, LIST)],
                    expect="abort")
    assert "not the waiting page" in got[1], got[1]
    assert "122/255" in got[1], got[1]
    assert host.shots == ["setup-failed"], host.shots

    # 4. a blank/error page is dark, not the waiting page and not the table.
    host, got = run("communication error", [(122, WAITING), (5, FADE)], expect="abort")
    assert "5/255" in got[1], got[1]
    assert host.shots == ["setup-failed"], host.shots

    # 5. a press that moves nothing is sent again (presses=2), then aborts.
    host, got = run("swallowed press", [(122, WAITING)], moves=False,
                    presses=3, expect="abort")
    assert host.presses == 3, host.presses
    assert "the waiting page at 122/255" in got[1], got[1]

    print("all stage-gate cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
