#!/usr/bin/env python3
"""Read the app's two consoles at MKDS's post-VS park, press, and read again.

The park is the state both this project's harness and its app reach after CUT
OFF + VS: a black picture on both consoles with the link live. This reads what
the control channel can see (the renderer's framebuffers and the emulator's own
registers) before and after input, so "the game is drawing black" and "the game
answered the pad" are separate facts rather than one screenshot.

    tools/scripts/mkds-park-probe.py                 # read, press nothing
    tools/scripts/mkds-park-probe.py --press a --press start
    tools/scripts/mkds-park-probe.py --touch 1:128:96 --touch 2:128:96
"""

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from appctl import Channel  # noqa: E402


def snap(ch, label):
    out = [f"--- {label} ---"]
    for p in (1, 2):
        pr = ch.call(cmd="probe", player=p)
        fb, cp = pr["framebuffer"], pr["copy"]
        w = pr.get("wifi") or {}
        out.append(
            f"p{p} fb top {fb['top']['crc']} ({fb['top']['mean']}/{fb['top']['black_pct']}% black) "
            f"bottom {fb['bottom']['crc']} ({fb['bottom']['mean']}/{fb['bottom']['black_pct']}% black) · "
            f"copy top {cp['top']['crc']} changed {cp['beats']}x last {cp['last_beat_frame']} · "
            f"pc9 {pr.get('pc9')} pc7 {pr.get('pc7')} frames {pr.get('frames')} · "
            f"powcnt1 {pr['powcnt1']} asleep {pr['asleep']} · "
            f"role {w.get('role')} irq cpu line {w.get('irq_cpu_line')} "
            f"raised/acked {w.get('raised')}/{w.get('acked')} → "
            f"IF {w.get('w_if')} IE {w.get('w_ie')} pending {w.get('w_pending')} · "
            f"cmds rx/tx {w.get('cmds_rx')}/{w.get('cmds_tx')} arms/answered {w.get('arms')}/{w.get('answered')} "
            f"slots b {w.get('txslots')}"
        )
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=7373)
    ap.add_argument("--press", action="append", default=[], help="buttons to hold on BOTH consoles")
    ap.add_argument("--press-player", action="append", default=[], help="PLAYER:BUTTONS, e.g. 1:a+start")
    ap.add_argument("--touch", action="append", default=[], help="PLAYER:X:Y to tap on the bottom screen")
    ap.add_argument("--ms", type=int, default=400)
    ap.add_argument("--settle", type=float, default=2.0, help="seconds to wait after each input")
    ap.add_argument("--reps", type=int, default=1, help="how many times to repeat the whole battery")
    ap.add_argument("--samples", type=int, default=0,
                    help="read this many snapshots before pressing anything, one every --interval s")
    ap.add_argument("--interval", type=float, default=0.5, help="seconds between --samples snapshots")
    args = ap.parse_args()

    ch = Channel(args.port, timeout=60)
    for line in snap(ch, "before"):
        print(line, flush=True)

    # A parked console is not a dead one until the frame counter says so, and one
    # snapshot cannot tell "executing a black frame" from "the emulation thread
    # has stopped and the app is re-serving its last copy". Sampling the same
    # reads several times answers that: `frames` must climb, the program
    # counters must be *somewhere* the game runs, and the wifi block's raised and
    # acked counters must move if the game still services its radio. The app's
    # own copy is counted apart (`copy beats`): a beat only happens when the
    # picture the host is handed differs from the last one, so a beat count that
    # climbs while both crcs stay put is the renderer producing fresh black.
    if args.samples:
        t0 = time.time()
        for i in range(args.samples):
            if i:
                time.sleep(args.interval)
            print(f"### sample {i + 1}/{args.samples} at t+{time.time() - t0:.1f}s", flush=True)
            for line in snap(ch, f"sample {i + 1}"):
                print(line, flush=True)
    for rep in range(args.reps):
        for buttons in args.press:
            for p in (1, 2):
                print(f"  press {buttons} on p{p}", flush=True)
                ch.call(cmd="press", player=p, buttons=buttons, ms=args.ms)
            time.sleep(args.settle)
            for line in snap(ch, f"rep{rep + 1} after press {buttons} on both"):
                print(line, flush=True)
        for spec in args.press_player:
            player, buttons = spec.split(":", 1)
            print(f"  press {buttons} on p{player}", flush=True)
            ch.call(cmd="press", player=int(player), buttons=buttons, ms=args.ms)
            time.sleep(args.settle)
            for line in snap(ch, f"rep{rep + 1} after press {buttons} on p{player}"):
                print(line, flush=True)
        for spec in args.touch:
            player, x, y = (int(v) for v in spec.split(":"))
            print(f"  touch {x},{y} on p{player}", flush=True)
            ch.call(cmd="touch", player=player, x=x, y=y, ms=args.ms)
            time.sleep(args.settle)
            for line in snap(ch, f"rep{rep + 1} after touch ({x},{y}) on p{player}"):
                print(line, flush=True)
    ch.close()


if __name__ == "__main__":
    main()
