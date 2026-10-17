#!/usr/bin/env python3
"""Does each console read its own input, and only its own? (the 4-player question)

Local splitscreen is several people at one keyboard, so the property that has to
hold is per-console: a press made for console N shows up in console N's own
keypad and in nobody else's. Every emulated console reports that state in the
hardware register its game itself reads (`0x04000130`, active-low: bit 0 is A,
0x3FF is nothing pressed), and the control channel's `mem` reads it per console -
so this is the game's own ABI, not a picture, and it works on any title.

Two surfaces, because they are different paths into the same consoles:

  channel   the control channel's own input (`set_input` -> the console's own
            state): per-console by construction
  keyboard  a real KeyboardEvent, the surface a person uses; the page routes it
            by the console whose keymap names the code

For every console it holds that console's own key (or channel press), reads all
four consoles' keypad registers, releases, and reads again. A pass is: exactly
one console's register shows the button while it is held (the one that was
driven), and all four read `0x3FF` again after the release.

    tools/scripts/four-console-input.py                      # 4 consoles, both surfaces
    tools/scripts/four-console-input.py --surface keyboard
"""

import argparse
import json
import os
import socket
import sys

KEYPAD = 0x04000130
IDLE = 0x3FF
DEFAULT_PORT = int(os.environ.get("MD_CONTROL_PORT") or 7373)


class Channel:
    """One request line, one response line (the app's control channel)."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=60.0)
        self.sock.settimeout(60.0)
        self.reader = self.sock.makefile("rb")
        self.writer = self.sock.makefile("wb")
        self.next_id = 1

    def call(self, **req):
        req["id"] = self.next_id
        self.next_id += 1
        self.writer.write((json.dumps(req) + "\n").encode())
        self.writer.flush()
        line = self.reader.readline()
        if not line:
            raise SystemExit("the control channel closed the connection")
        out = json.loads(line)
        if not out.get("ok"):
            raise SystemExit(f"channel error: {out.get('error') or out}")
        return out.get("data")


def keypads(ch, players):
    """The keypad register of every console, as its own game reads it.

    `mem` answers the console's own words in order; the keypad lives in the low
    half of the word at 0x04000130 (bits 0-9 = A..L, active low)."""
    got = []
    for p in range(players):
        out = ch.call(cmd="mem", player=p + 1, addr=f"{KEYPAD:08X}", len=4)
        got.append(int(out["words"][0], 16) & 0xFFFF)
    return got


def key_for_button(ch, players, bit):
    """Each console's own code for one DS button, from the session's own keymaps."""
    km = json.loads(ch.call(cmd="eval", expr="JSON.stringify(session.keymaps)"))
    return [next((c for c, b in km[p] if b == bit), None) for p in range(players)]


def advance(ch, frames):
    st = ch.call(cmd="status")
    if not st.get("active"):
        raise SystemExit("no session running")
    start = st["frames"][0]["seq"]
    import time

    deadline = time.time() + max(15.0, frames / 59.826 * 2.0 + 5.0)
    while time.time() < deadline:
        st = ch.call(cmd="status")
        if st["frames"][0]["seq"] - start >= frames:
            return
        time.sleep(0.25)
    raise SystemExit(f"console 1 did not run {frames} frames within the deadline")


def keypads_until(ch, players, want_p, want=None, deadline_s=0.6):
    """Poll until console `want_p` reads anything but idle (want=None), or until
    `want` on that console. A press is applied by the console's own frame loop,
    so one read can land before the frame that carries it - the verdict is taken
    once the console's own register says the press arrived."""
    import time

    end = time.time() + deadline_s
    last = keypads(ch, players)
    while time.time() < end:
        ok = (last[want_p] != IDLE) if want is None else (last[want_p] == want)
        if ok:
            return last
        time.sleep(0.03)
        last = keypads(ch, players)
    return last


def press(ch, surface, player, code, mask, down):
    if surface == "channel":
        ch.call(cmd="input", player=player + 1, buttons=mask if down else 0, touch=None)
    else:
        ch.call(cmd="key", code=code, down=down)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--players", type=int, default=4)
    ap.add_argument("--button", type=lambda v: int(v, 0), default=1, help="DS button mask (1 = A)")
    ap.add_argument("--surface", choices=["channel", "keyboard", "both"], default="both")
    ap.add_argument("--settle", type=int, default=0, help="frames to let each console run first")
    ap.add_argument("--title", default="", help="label for the report line")
    args = ap.parse_args()

    ch = Channel(args.port)
    if args.settle:
        advance(ch, args.settle)

    surfaces = ["channel", "keyboard"] if args.surface == "both" else [args.surface]
    codes = key_for_button(ch, args.players, args.button) if "keyboard" in surfaces else [None] * args.players

    idle = keypads(ch, args.players)
    if any(v != IDLE for v in idle):
        print(f"  WARNING: keypads are not idle before the test: {[hex(v) for v in idle]}")

    fails = 0
    for surface in surfaces:
        for p in range(args.players):
            if surface == "keyboard" and not codes[p]:
                print(f"  {surface} p{p + 1}: no key on this console maps mask {args.button:#x}")
                fails += 1
                continue
            press(ch, surface, p, codes[p], args.button, True)
            held = keypads_until(ch, args.players, p)
            press(ch, surface, p, codes[p], args.button, False)
            after = keypads_until(ch, args.players, p, want=IDLE)

            want_own = IDLE & ~args.button
            own_ok = held[p] == want_own
            others_ok = all(held[i] == IDLE for i in range(args.players) if i != p)
            released_ok = all(v == IDLE for v in after)
            ok = own_ok and others_ok and released_ok
            if not ok:
                fails += 1
            label = codes[p] if surface == "keyboard" else f"mask {args.button:#x}"
            print(f"  {args.title}{surface} p{p + 1} ({label}): "
                  f"held={[hex(v) for v in held]} released={[hex(v) for v in after]} "
                  f"-> {'PASS' if ok else 'FAIL'}"
                  f"{'' if own_ok else ' (own console did not read it)'}"
                  f"{'' if others_ok else ' (MISROUTED to another console)'}"
                  f"{'' if released_ok else ' (did not release)'}")
    print(f"  {args.title}{args.players} consoles: {'all per-console, none misrouted' if not fails else f'{fails} FAILURE(S)'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
