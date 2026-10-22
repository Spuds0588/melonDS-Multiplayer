#!/usr/bin/env python3
"""Measure a running session's speed and stability: per-console fps, CPU, events.

Why this file exists: the health line in the app log is a five-second window,
so a run's *distribution* (the low frames a race hits, the spike in a load
screen) is not in it, and the only CPU number a session ever reported was a
hand-read `ps` at the end. A four-console room needs both for the whole run,
plus a census of the events that end rooms - a stall, a kick, a deauth, a
console the wedge detector called out - because those are what "stable" means.

It reads the app through its own control channel (`status`: each console's
emulated frame counter) and through /proc (own process, its threads, and its
child processes, whether or not they are threads of this process). Nothing is
injected and nothing is touched in the emulator, so a measurement is safe on a
session someone is playing.

    tools/scripts/room-bench.py --seconds 120 --tag mkds_room
    tools/scripts/room-bench.py --log logs/x_app.log --out logs/x_bench.json

The app's pid is read from the log's own `[supervisor] app pid N started` line,
so the log has to be the one `tools/run_app.sh` wrote (it is by default).

Exit 0 when the samples were taken; 2 when the session or its log never
appeared (a measurement that produced no numbers must not look like one that
measured zeroes).
"""

import argparse
import json
import os
import re
import statistics
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from appctl import Channel  # noqa: E402

HZ = os.sysconf("SC_CLK_TCK")

# What the app writes when a room loses a console, a console is kicked, a
# console's radio goes on or off, or a game stops moving while frames run. Each
# is a state change in the session, not a counter, which is why the census
# counts lines and keeps the first and last of each.
EVENTS = [
    ("stall", r"p\d+ stalled"),
    ("stall-left-alone", r"p\d+ stalled \(window \d+\) - left alone"),
    ("kick", r"resynced its bus state"),
    ("wedge", r"p\d+ looks wedged"),
    ("lockstep-dissolved", r"lockstep dissolved"),
    ("console-stopped", r"console stopped on its own"),
    ("heartbeat-missed", r"heartbeat: a console produced no frames"),
    ("deauth-sent", r"\[mp\] deauth sent"),
    ("wireless-off", r"wireless off"),
    ("wireless-on", r"wireless ON"),
    ("picture-still", r"the picture has not changed in \d+ frames"),
    ("link-verified", r"link verified"),
]


def app_pid(log, wait=20.0):
    """The emulator's own pid, from the supervisor's startup line.

    Read with a bounded wait: a launcher and a bench started in the same
    command race each other, and the supervisor's first line can land after the
    control port is already answering (measured on the 2026-10-22 run 4, where
    an instant read found no pid line and the measurement refused to start).
    """
    deadline = time.time() + wait
    while True:
        try:
            with open(log, errors="replace") as fh:
                for ln in fh:
                    m = re.search(r"\[supervisor\] app pid (\d+) started", ln)
                    if m:
                        return int(m.group(1))
        except OSError:
            pass
        if time.time() >= deadline:
            return None
        time.sleep(0.5)


def ticks(pid):
    """(own process ticks, {thread name: ticks}, tree ticks) for this pid."""
    own, per_thread = 0, {}
    try:
        with open(f"/proc/{pid}/stat") as fh:
            fields = fh.read().rsplit(")", 1)[1].split()
        own = int(fields[11]) + int(fields[12])
    except OSError:
        return None, {}, 0
    try:
        tids = os.listdir(f"/proc/{pid}/task")
    except OSError:
        tids = []
    for tid in tids:
        try:
            with open(f"/proc/{pid}/task/{tid}/stat") as fh:
                f = fh.read().rsplit(")", 1)[1].split()
            with open(f"/proc/{pid}/task/{tid}/comm") as fh:
                comm = fh.read().strip()
        except OSError:
            continue
        per_thread[comm] = per_thread.get(comm, 0) + int(f[11]) + int(f[12])
    # Child processes (a webview's renderer and friends): these are not threads
    # of the app, so the process's own tick count cannot see them.
    tree, stack = own, []
    try:
        for tid in tids:
            with open(f"/proc/{pid}/task/{tid}/children") as fh:
                stack.extend(int(c) for c in fh.read().split())
    except OSError:
        pass
    seen = set()
    while stack:
        c = stack.pop()
        if c in seen:
            continue
        seen.add(c)
        try:
            with open(f"/proc/{c}/stat") as fh:
                f = fh.read().rsplit(")", 1)[1].split()
            tree += int(f[11]) + int(f[12])
        except OSError:
            continue
        for tid in os.listdir(f"/proc/{c}/task") if os.path.isdir(f"/proc/{c}/task") else []:
            try:
                with open(f"/proc/{c}/task/{tid}/children") as fh:
                    stack.extend(int(x) for x in fh.read().split())
            except OSError:
                pass
    return own, per_thread, tree


def pct(values, q):
    if not values:
        return None
    s = sorted(values)
    i = min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))
    return s[i]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=120)
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--port", type=int, default=7373)
    ap.add_argument("--log", default=os.path.join(ROOT, "logs/md_app.log"))
    ap.add_argument("--out", default=None, help="write the samples here as JSON")
    args = ap.parse_args()

    pid = app_pid(args.log)
    if pid is None:
        print(f"no app pid in {args.log}: was the session started by tools/run_app.sh?",
              file=sys.stderr)
        return 2
    ch = Channel(args.port, timeout=30)
    st = ch.call(cmd="status")
    if not st.get("active"):
        print("the app has no running session", file=sys.stderr)
        return 2
    players = [f["player"] for f in st["frames"]]
    print(f"session: {st['players']} console(s), {st['rom']}, app pid {pid}, "
          f"link mask {st['link_mask']:#x} - sampling {args.seconds:.0f}s")

    base_ticks = ticks(pid)
    if base_ticks[0] is None:
        print(f"pid {pid} is gone", file=sys.stderr)
        return 2
    base_frames = {f["player"]: f["seq"] for f in st["frames"]}
    last_frames, last_t = base_frames, time.time()
    base_t = last_t
    prev_tree = base_ticks[2]
    samples = []

    while time.time() - base_t < args.seconds:
        time.sleep(args.interval)
        st = ch.call(cmd="status")
        if not st.get("active"):
            print("the session ended during the measurement", file=sys.stderr)
            break
        now = time.time()
        cur = {f["player"]: f["seq"] for f in st["frames"]}
        own, per_thread, tree = ticks(pid)
        if own is None:
            print("the app exited during the measurement", file=sys.stderr)
            break
        dt = now - last_t
        samples.append({
            "t": round(now - base_t, 2),
            "fps": {p: round((cur[p] - last_frames[p]) / dt, 1) for p in players},
            "mask": st["link_mask"],
            "cores": round((tree - prev_tree) / HZ / dt, 2),
        })
        prev_tree = tree
        last_frames, last_t = cur, now

    total = time.time() - base_t
    end_ticks = ticks(pid)
    if end_ticks[0] is None:
        print("the app exited before the measurement finished", file=sys.stderr)
        return 2
    frames = {p: 0 for p in players}
    # Re-derive each console's whole-run frames from one more status read so the
    # run's total does not depend on the sampling loop's last sample.
    st = ch.call(cmd="status")
    if st.get("active"):
        for f in st["frames"]:
            frames[f["player"]] = f["seq"] - base_frames[f["player"]]

    print(f"\n=== {total:.1f}s of wall clock, {len(samples)} samples, "
          f"final link mask {st.get('link_mask', 0):#x}")
    for p in players:
        vals = [s["fps"][p] for s in samples if p in s["fps"]]
        print(f"p{p}: {frames[p]} emulated frames = {frames[p] / total:.1f} fps average"
              + (f" | min {min(vals):.1f} p25 {pct(vals, .25):.1f} median {statistics.median(vals):.1f}"
                 f" p75 {pct(vals, .75):.1f} max {max(vals):.1f}" if vals else ""))
    # Lockstep: every console runs the same frames per window if the barrier
    # holds. The spread of the per-window spread is what shows a break.
    spreads = [max(s["fps"].values()) - min(s["fps"].values()) for s in samples
               if len(s["fps"]) > 1]
    if spreads:
        print(f"lockstep: per-window fps spread between consoles max {max(spreads):.1f}, "
              f"median {statistics.median(spreads):.1f} "
              f"({sum(1 for v in spreads if v > 0.5)} of {len(spreads)} windows differ by >0.5 fps)")
    own_c = (end_ticks[0] - base_ticks[0]) / HZ / total
    tree_c = (end_ticks[2] - base_ticks[2]) / HZ / total
    print(f"cpu: app {own_c:.2f} cores of {os.cpu_count()} | with its children {tree_c:.2f}")
    per_thread = {}
    for name, val in end_ticks[1].items():
        per_thread[name] = per_thread.get(name, 0) + val - base_ticks[1].get(name, 0)
    rows = sorted(((v / HZ / total, k) for k, v in per_thread.items() if v / HZ / total > 0.05),
                  reverse=True)
    for cores, name in rows:
        print(f"  {name:<24} {cores:5.2f} cores  {100 * cores / own_c:4.0f}%")

    text = ""
    try:
        with open(args.log, errors="replace") as fh:
            text = fh.read()
    except OSError:
        pass
    print("\nevents in the app log (whole run, not only this window):")
    found = {}
    for label, pat in EVENTS:
        hits = re.findall(pat, text)
        found[label] = len(hits)
        print(f"  {label:<18} {len(hits)}")
    if args.out:
        with open(args.out, "w") as fh:
            json.dump({"samples": samples, "frames": frames, "seconds": total,
                       "cores": own_c, "tree_cores": tree_c,
                       "threads": {k: round(v / HZ / total, 3) for k, v in per_thread.items()},
                       "events": found}, fh, indent=1)
        print(f"(samples written to {args.out})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
