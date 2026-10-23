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
    tools/scripts/tetris-4p.py --players 4 --host 1 --start    # guests ready, then the host

Screenshots of every stage go to logs/shots/tetris4p-<stage>-p<N>.png. The
driver never guesses that a stage was reached: the caller reads the pictures
(or the link counters in logs/md_app.log) and the next pass can re-run the same
file. This is deliberately the reusable route, not a one-off tap list.

The start sequence is the one measured to produce a match (2026-10-22, the
proven replay of the earlier control): each guest's A opens its own rules
panel, the host's A twice takes it to "please wait", and each guest's A again
confirms its rules - which is the press that lets the host's game leave its
ready loop. Every press is *proven* by that console's own panel moving and
re-sent until it does; the same order sent blind left both consoles on the
table, because a swallowed press and a landed one are indistinguishable from
the outside.
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

    def press_proven(self, mask=0x1, label="", attempts=5, patience=2.2):
        """Send a press until this console's own bottom panel moves.

        The room swallows a press made within a second or two of a phase
        change, and a swallowed press changes nothing on the console that was
        supposed to take it - so the panel, not the send, decides. Re-sending
        is safe: a press that already landed has moved the panel and the loop
        has exited before the second one is sent.
        """
        before = self.sig()
        for attempt in range(1, attempts + 1):
            self.press(mask)
            if self.changed(before, patience):
                if label:
                    print(f"  p{self.p}: {label} (attempt {attempt})", flush=True)
                return attempt
        raise SystemExit(f"p{self.p}: {label or 'press'} never moved this console's panel "
                         "- see the stage shots")

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


def emu_seconds(ch):
    """The group's emulated clock, from the frame counters the host reports.

    The room's own patience is written in emulated seconds (the title's deauth
    timer), while every wall-clock sleep in this route is a wall-clock sleep;
    at 18fps and at 60fps the same sleep is a different number of emulated
    seconds, which is how the four-console route came to fit inside the room's
    window on a slow group and miss it on a fast one. Stamping each step with
    this makes the two clocks comparable in the route's own output.
    """
    st = ch.call(cmd="status")
    frames = [f["seq"] for f in st.get("frames", []) if "seq" in f]
    return min(frames) / 59.826 if frames else 0.0


def stamp(ch, label):
    print(f"  t={emu_seconds(ch):6.2f} emulated s | {label}", flush=True)


def reenter_room(pad):
    """Walk a guest back into the host's room after the title's close put it
    back on the join browser.

    Measured 2026-10-23: the room closes once mid-handshake and re-forms, and the
    guests handed that close return to their own join browser - the host's mask
    reads 000E (all three in its room) while all three guests are still drawing
    the browser page, and `A` moves none of their panels in five attempts. The
    browser's own entry is the sequence this route already uses: JOIN's two
    touches, then A once the room list has been drawn. Recovering per console
    rather than for the whole batch is what keeps a guest that is already in the
    room from being tapped out of it.
    """
    pad.tap2(MENU_JOIN)
    time.sleep(0.8)
    pad.press(0x1)
    time.sleep(0.6)


def press_all_proven(pads, mask=0x1, label="", attempts=5, patience=2.2, recover=None):
    """One press to every console at once, proven landed on each.

    The guests' rule panels do not depend on each other, so the presses go out
    as a batch and the proof runs per console afterwards: a console that moved
    drops out of the batch and the rest are pressed again. The serial form this
    replaces paid up to 5 attempts x 2.2s *per console* - about 13s of the
    room's emulated-second patience for three guests, which is a third of the
    window the title gives the room.
    """
    pending = list(pads)
    before = {p.p: p.sig() for p in pending}
    recovered = {p.p: 0 for p in pending}
    for attempt in range(1, attempts + 1):
        for p in pending:
            p.press(mask)
        moved = []
        start = time.time()
        while pending and time.time() - start < patience:
            time.sleep(0.2)
            still = []
            for p in pending:
                if p.diff(p.sig(), before[p.p]) > 6.0:
                    moved.append(p.p)
                else:
                    still.append(p)
            pending = still
        moved_names = ",".join(f"p{n}" for n in moved) or "-"
        silent_names = ",".join(f"p{p.p}" for p in pending) or "-"
        print(f"  batch '{label}' attempt {attempt}: {moved_names} moved, {silent_names} silent",
              flush=True)
        if not pending:
            return attempt
        # A console whose panel never moved is not hearing the button: the
        # room's close put its game back on the join browser. Walk it back in
        # and press again, twice at most so a genuinely stuck game still fails
        # the route instead of being tapped forever.
        if recover:
            for p in list(pending):
                if recovered.get(p.p, 0) < 2:
                    recovered[p.p] = recovered.get(p.p, 0) + 1
                    print(f"  p{p.p}: in the browser - re-entering the room", flush=True)
                    recover(p)
            before = {p.p: p.sig() for p in pending}
    raise SystemExit(f"batch '{label}': {len(pending)} console(s) never moved - see the stage shots")


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
    stamp(ch, "all guests out of the join browser")

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
    host.press_proven(0x1, "setup table")
    time.sleep(1.0)
    for p in pads.values():
        p.shot("setup")

    if not args.start:
        print("stopping at the setup table (no --start)", flush=True)
        return 0

    # 5. readies, in the order measured to start a match (2026-10-22 replay of
    #    the working control: guest A, host A, host A, guest A at 1-2s spacing).
    #    Each guest's first A opens its own rules panel, the host's two presses
    #    take it to "please wait", and each guest's second A confirms its rules
    #    - the press the host's game waits on. Sent blind in this same order the
    #    room stays on the table, because the presses are swallowed and nothing
    #    reports it; proven one at a time it starts on the first attempt.
    stamp(ch, "all guests in the room, before the rules presses")
    press_all_proven(guests, 0x1, "rules panel open", recover=reenter_room)
    host.press_proven(0x1, "host to please-wait")
    time.sleep(1.0)
    # The second host press is the one the room swallows: it is already on
    # "please wait", so requiring this panel to move would abort a good route
    # (measured 2026-10-22 - tetris-confirm's proof passed it in the run that
    # started a match, and the driver aborted on it in the next run).
    host.press(0x1)
    time.sleep(1.0)
    press_all_proven(guests, 0x1, "rules confirmed", recover=reenter_room)
    stamp(ch, "every guest's rules confirmed")

    # Give the room time to start, then photograph every console. The verdict is
    # read from the game's own protocol afterwards - the host's log carries
    # `body[6]=F6 00` and then `F6 00 0n 00 04 9D` with MD_LINK_VERBOSE=1 - not
    # from these pictures, which only say which panel each console is drawing.
    time.sleep(4.0)
    for p in pads.values():
        p.shot("match")
    print("match screen captured on every console", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
