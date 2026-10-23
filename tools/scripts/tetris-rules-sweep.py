#!/usr/bin/env python3
"""Sweep every input on a guest's Tetris DS rules panel until its answer flips.

The three-console room forms and all three consoles sit on the three-player
table with their own NAME / ITEMS / HANDICAP / TEAM panels, but the host's
`05 9D` roster never completes. Measured 2026-10-22: a guest whose rules are set
answers `20 A0` in its table answer (and `01 8D` in the two-console form), one
whose are not answers `20 20`, and the A presses that set it at two consoles left
a three-console guest at `20 20`. Guessing a second time is what this replaces:
the panel's touch grid is tried first (a touch on a game panel does not leave
it), then every button and the plausible ordered pairs, each from the panel's
recorded state where the bridge will restore it, and the guest's own answer is
read out of the link log after every attempt.

    tools/scripts/tetris-rules-sweep.py --players 2,3 --log logs/sweep_run.log

Reuses `menu-map.py`'s machinery for what it already solves: the bottom-panel
signature that tells a menu from itself, the settled read, and the action
applier. Exit status is 0 only when every requested console's answer carried the
rules-set bit before the script finished.

The oracle is the log, because the answer body is what the host's own game reads
and the control channel does not carry it: after each attempt the last
`answer sent` line for that console is re-read and its body checked for `20 A0`
(written as the three halfwords the game puts on the wire) or `01 8D`.
"""

import argparse
import importlib.util
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from appctl import DEFAULT_PORT, Channel  # noqa: E402


def load_menu_map():
    path = os.path.join(HERE, "menu-map.py")
    spec = importlib.util.spec_from_file_location("menumap", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


BUTTONS = ["a", "b", "start", "select", "x", "y", "l", "r",
           "up", "down", "left", "right"]
PAIRS = ["a+a", "b+a", "start+a", "right+a", "a+start", "a+b"]


def body_of(line):
    m = re.search(r"body\[\d+\]=(.*)$", line.strip())
    return m.group(1).strip() if m else ""


def last_answer(logpath, player):
    """The last `answer sent` line this console wrote, read from the log tail."""
    try:
        with open(logpath, "rb") as fh:
            fh.seek(0, os.SEEK_END)
            size = fh.tell()
            fh.seek(max(0, size - 1_500_000))
            data = fh.read().decode("utf-8", "replace").splitlines()
    except OSError as err:
        raise SystemExit(f"cannot read the link log {logpath}: {err}")
    tag = f"melonDS p{player}:Debug"
    for line in reversed(data):
        if tag in line and "answer sent" in line:
            return line
    return ""


def rules_set(logpath, player):
    body = body_of(last_answer(logpath, player))
    return ("20 A0" in body) or ("01 8D" in body), body


def touch_grid(step):
    """Every touch point on this console's bottom screen, panel included."""
    xs = list(range(step // 2, 256, step))
    ys = list(range(192 + step // 2, 384, step))
    return [(x, y - 192) for y in ys for x in xs]


def sweep_player(sweep, ch, player, logpath, wait, touch_step, restorable):
    """Sweep one console's panel. Returns (rules_set, restore_still_works)."""
    print(f"== p{player}: panel snapshot", flush=True)
    ch.call(cmd="snapshot", player=player, name=f"rules-{player}")
    ref = sweep.sig()
    ok, body = rules_set(logpath, player)
    print(f"   before: {body or '-'} (rules set: {ok})", flush=True)
    if ok:
        return True, restorable

    restorable_now = restorable

    def back_to_panel():
        nonlocal restorable_now
        if not restorable_now:
            return True
        try:
            ch.call(cmd="restore", player=player, name=f"rules-{player}")
        except SystemExit as err:
            print(f"   restore refused ({err}) - continuing without it", flush=True)
            restorable_now = False
            return False
        time.sleep(0.3)
        return True

    def attempt(label, action):
        sweep.apply(action)
        time.sleep(wait)
        ok, body = rules_set(logpath, player)
        on_panel = sweep.dist(sweep.sig(), [ref]) < 6.0
        print(f"  {label}: {body or '-'}{'  <-- RULES SET' if ok else ''}"
              f"{'' if on_panel else '  (left the panel)'}", flush=True)
        return ok, on_panel

    # 1. the panel's touch grid - a touch on the panel does not leave it, so
    #    this phase needs no restore between attempts
    for n, xy in enumerate(touch_grid(touch_step), 1):
        ok, on_panel = attempt(f"[touch {n}/{len(touch_grid(touch_step))}] {xy[0]},{xy[1]}",
                               ("touch", xy))
        if ok:
            return True, restorable_now
        if not on_panel and not back_to_panel():
            print("   leaving the panel without a restore - touch phase stops", flush=True)
            break

    # 2. buttons and ordered pairs, each from the panel's recorded state
    for label in BUTTONS + PAIRS:
        if not back_to_panel():
            break
        if sweep.dist(sweep.sig(), [ref]) >= 6.0:
            print(f"  [press {label}] not on the panel - stopping the button phase",
                  flush=True)
            break
        for part in label.split("+"):
            ok, on_panel = attempt(f"[press {label}] {part}", ("press", part))
            if ok:
                return True, restorable_now
        if not on_panel and not restorable_now:
            print("   leaving the panel without a restore - button phase stops", flush=True)
            break
    return False, restorable_now


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--players", default="2",
                    help="comma-separated guest console numbers (default 2)")
    ap.add_argument("--log", required=True, help="the app's own verbose link log")
    ap.add_argument("--wait", type=float, default=0.7,
                    help="seconds between an input and the answer read (default 0.7)")
    ap.add_argument("--touch-step", type=int, default=32,
                    help="touch grid spacing in DS pixels (default 32 -> 8x6)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = ap.parse_args()

    mm = load_menu_map()
    ch = Channel(args.port)
    st = ch.call(cmd="status")
    if not st.get("active"):
        raise SystemExit("no session running - start the app with tools/run_app.sh first")
    print(f"session: link mask {st.get('link_mask')}, "
          f"players {[f['player'] for f in st.get('frames', [])]}", flush=True)

    outdir = os.path.abspath(os.path.join(HERE, "..", "..", "logs", "rules-sweep"))
    os.makedirs(outdir, exist_ok=True)
    sweep = mm.Sweep(ch, 1, outdir, settle=1.5, timeout=1.5, budget=0)

    results = {}
    restorable = True
    for p in [int(x) for x in args.players.split(",")]:
        sweep.player = p
        results[p], restorable = sweep_player(sweep, ch, p, args.log, args.wait,
                                              args.touch_step, restorable)
        if not restorable:
            print("   this session cannot put the panel back - remaining consoles "
                  "are swept without restores", flush=True)
        print(f"== p{p}: rules set = {results[p]}", flush=True)

    missing = [p for p, ok in results.items() if not ok]
    if missing:
        print(f"FAILED: no input set the rules bit for {missing}", flush=True)
        return 1
    print("every requested guest reports its rules set", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
