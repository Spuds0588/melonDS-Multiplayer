#!/usr/bin/env python3
"""Drive 2-4 Tetris DS consoles into one local-wireless Standard match.

The route was read off the game's own screens. Its main menu is a touch list
(bottom panel rows at y=40/70/96/124/152: SINGLE PLAYER, MULTIPLAYER, Wi-Fi,
OPTIONS, RECORDS), and every row takes *two* touches: the first moves the
highlight, the second enters. A first touch that only highlights looks exactly
like a first touch that entered, from the outside, so this driver does not try
to tell them apart - it paces the pair (1.2s, the gap measured working) and
verifies the *stage* afterwards with a screenshot a person can check.

    tools/scripts/tetris-4p.py --players 2 --host 1            # stop at the setup table
    tools/scripts/tetris-4p.py --players 4 --host 1 --start    # press A through the start

Screenshots of every stage go to logs/shots/tetris4p-<stage>-p<N>.png. The
driver never guesses that a stage was reached: the caller reads the pictures
(or the link counters in logs/md_app.log) and the next pass can re-run the same
file. This is deliberately the reusable route, not a one-off tap list.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from appctl import DEFAULT_PORT, Channel  # noqa: E402

# Bottom-screen coordinates, read off the game's panels.
MENU_MULTIPLAYER = (110, 68)
MENU_INVITE = (110, 50)     # host: the multiplayer screen's top button
MENU_JOIN = (110, 110)      # guest: the multiplayer screen's bottom button
HOST_STANDARD = (110, 56)   # host: first mode on the room screen
GAP = 1.2                   # seconds between the two touches of a menu row


class Pad:
    def __init__(self, ch, player):
        self.ch = ch
        self.p = player

    def tap(self, xy, ms=180):
        self.ch.call(cmd="touch", player=self.p, x=xy[0], y=xy[1], ms=ms)

    def tap2(self, xy, label=""):
        """A menu row's two touches: highlight, then enter."""
        self.tap(xy)
        time.sleep(GAP)
        self.tap(xy)
        time.sleep(0.6)
        if label:
            print(f"  p{self.p}: {label}", flush=True)

    def press(self, mask, ms=200):
        self.ch.call(cmd="press", player=self.p, buttons=mask, ms=ms)

    def sig(self):
        """Bottom-screen thumbnail: the part of this panel a menu lives in."""
        from PIL import Image

        out = self.ch.call(cmd="shot", name=f"tetris4p-sig-p{self.p}")
        im = Image.open(out["path"]).convert("L")
        left = (self.p - 1) * 256
        return im.crop((left, 192, left + 256, 384)).resize((64, 48), Image.BOX).tobytes()

    @staticmethod
    def diff(a, b):
        return sum(abs(x - y) for x, y in zip(a, b)) / len(a)

    def changed(self, before, timeout=2.0, threshold=6.0):
        """True when the bottom screen moves away from `before` within `timeout`."""
        start = time.time()
        while time.time() - start < timeout:
            time.sleep(0.15)
            if self.diff(self.sig(), before) > threshold:
                return True
        return False

    def shot(self, stage):
        out = self.ch.call(cmd="shot", name=f"tetris4p-{stage}-p{self.p}")
        print(f"  p{self.p}: {stage} -> {out['path']}", flush=True)


def boot_wait(ch, players, frames=900):
    start = time.time()
    seq = {}
    while time.time() - start < 60:
        st = ch.call(cmd="status")
        seq = {f["player"]: f["seq"] for f in st.get("frames", [])}
        if len(seq) >= players and min(seq.values()) >= frames:
            return seq
        time.sleep(0.5)
    raise SystemExit(f"consoles did not reach {frames} frames: {seq}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--players", type=int, default=4, choices=(2, 3, 4))
    ap.add_argument("--host", type=int, default=1, help="1-based console that hosts")
    ap.add_argument("--start", action="store_true",
                    help="press A through the room's setup table to start the match")
    ap.add_argument("--hold", action="store_true",
                    help="stop with every guest in the room and the host's start press "
                         "not made, so a caller can time that press itself")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = ap.parse_args()

    ch = Channel(args.port)
    st = ch.call(cmd="status")
    if not st.get("active"):
        raise SystemExit("no session running - start the app with tools/run_app.sh first")
    print(f"booted: {boot_wait(ch, args.players)}", flush=True)

    pads = {p: Pad(ch, p) for p in range(1, args.players + 1)}
    host = pads[args.host]
    for p in pads.values():
        p.shot("menu")

    # 1. every console: main menu -> the MULTIPLAYER screen
    for p in pads.values():
        p.tap2(MENU_MULTIPLAYER, "main menu -> MULTIPLAYER")
    for p in pads.values():
        p.shot("multiplayer")

    # 2. the host opens a Standard room (INVITE and STANDARD are already the
    #    highlighted row on their screens, so each is a single touch)
    host.tap(MENU_INVITE)
    time.sleep(1.5)
    host.tap(HOST_STANDARD)
    time.sleep(1.5)
    host.shot("host-waiting")
    print(f"p{args.host}: room open (Standard)", flush=True)

    # 3. guests search together, then all press A together.
    #
    # Serialised per guest this takes ~10s each, and the room's own patience is
    # 32-47s from the moment a guest associates (the measured deauth timer): the
    # first guest is already close to giving up by the time the last one is in,
    # and its own game writes its `reason 0003` and leaves. Phases instead of
    # guests - every console does each step in the same couple of seconds - keep
    # the whole party inside that window.
    guests = [p for p in pads.values() if p is not host]
    # The screen they are on *before* JOIN: the final check needs to know that
    # they left it, or a tap that only bounced the highlight reads as a join.
    mp_page = {p.p: p.sig() for p in guests}
    for p in guests:
        p.tap2(MENU_JOIN, "MULTIPLAYER -> JOIN")
    time.sleep(3.0)  # the room list renders when the host's beacon is heard
    for attempt in range(3):
        before = {p.p: p.sig() for p in guests}
        for p in guests:
            p.press(0x1)
        time.sleep(0.4)
        moved = [p for p in guests if p.changed(before[p.p], 2.0)]
        print(f"  join attempt {attempt + 1}: {len(moved)} of {len(guests)} answered A",
              flush=True)
        if len(moved) == len(guests):
            break
    for p in guests:
        p.shot("joined")
        if p.diff(p.sig(), mp_page[p.p]) < 6.0:
            raise SystemExit(
                f"p{p.p}: still on the multiplayer page - the JOIN touches never "
                "landed (see joined shot)")
    print(f"{len(guests)} guest(s) left the join browser", flush=True)

    if args.hold:
        for p in pads.values():
            p.shot("in-room")
        print("holding: every guest is in the room; the host's A has not been pressed",
              flush=True)
        return 0

    # 4. the host's A turns the waiting screen into the setup table, but only
    #    once each guest's own game has finished associating: pressed too early
    #    it is swallowed (measured 2026-10-22 - the same press that opens the
    #    table 6s after a guest's accept does nothing 1.5s after it). So it is
    #    pressed until the host's own panel changes, not once.
    before = host.sig()
    for attempt in range(6):
        host.press(0x1)
        if host.changed(before, 2.2):
            print(f"p{args.host}: setup table after {attempt + 1} press(es)", flush=True)
            break
    else:
        raise SystemExit("p%s: the host's panel never opened the setup table "
                         "(see setup shot)" % args.host)
    time.sleep(1.0)
    for p in pads.values():
        p.shot("setup")

    if not args.start:
        print("stopping at the setup table (no --start)", flush=True)
        return 0

    # 5. every player readies on the table (the last ready starts the match).
    #    A console whose table has not arrived yet swallows the press, so each
    #    one is pressed until its own panel leaves the table.
    for p in pads.values():
        p.press(0x1)
        time.sleep(1.5)
    time.sleep(3.0)
    for p in pads.values():
        p.shot("match")
    print("match screen captured on every console", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
