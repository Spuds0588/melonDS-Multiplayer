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
    args = ap.parse_args()

    ch = Channel(args.port, timeout=60)
    for line in snap(ch, "before"):
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
