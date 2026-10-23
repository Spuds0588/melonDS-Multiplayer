#!/usr/bin/env python3
"""Reply-window latency: how long a window waited, and what it left owed.

Reads the verbose `reply window: p1 closed (reason) after Xus wall,
collected=YYYY of ZZZZ, still owed WWWW (a,b)` lines and the `ack sent` lines
of one session, and prints

  * closes by reason, with the wall-time distribution per reason,
  * which AIDs were still owed at close, per window, in order,
  * for each owed aid, whether any later window collected it (the next close's
    `collected` mask covers that aid),
  * the ack `clientfail` rate.

    tools/scripts/mp-window-latency.py logs/lat3.log [...]
"""
import re
import sys
from collections import Counter, defaultdict

closed = re.compile(
    r"reply window: p(\d+) closed \((.*?)\) after (\d+)us wall, "
    r"collected=([0-9A-F]{4}) of ([0-9A-F]{4}), still owed ([0-9A-F]{4}) \(([^)]*)\)")
ack = re.compile(r"ack sent: cmdcount \d+, clientfail ([0-9A-F]{4})")


def pct(vals, q):
    if not vals:
        return 0
    vals = sorted(vals)
    return vals[min(len(vals) - 1, int(q * (len(vals) - 1)))]


for path in sys.argv[1:]:
    lines = open(path, "rb").read().decode("utf-8", "replace").splitlines()
    windows = []
    acks = []
    for ln in lines:
        m = closed.search(ln)
        if m:
            windows.append({
                "inst": int(m.group(1)),
                "why": m.group(2),
                "us": int(m.group(3)),
                "got": int(m.group(4), 16),
                "asked": int(m.group(5), 16),
                "owed": int(m.group(6), 16),
                "owed_list": m.group(7),
            })
            continue
        m = ack.search(ln)
        if m:
            acks.append(m.group(1))

    print(f"== {path}")
    print(f"   {len(windows)} windows, {len(acks)} acks")
    by = defaultdict(list)
    for w in windows:
        by[w["why"]].append(w)
    for why, ws in sorted(by.items(), key=lambda kv: -len(kv[1])):
        times = [w["us"] for w in ws]
        print(f"   {len(ws):5} {why:24} wall us: p50 {pct(times,.5):6} "
              f"p90 {pct(times,.9):6} max {max(times):7}  "
              f"owed-mask non-zero: {sum(1 for w in ws if w['owed'])}")

    owed_aids = Counter()
    for w in windows:
        for a in w["owed_list"].split(","):
            a = a.strip()
            if a and a != "none":
                owed_aids[int(a)] += 1
    print(f"   owed at close, by AID: {dict(sorted(owed_aids.items()))}")

    # Did a later window collect an owed aid? Look at the next window's `got`.
    late = Counter()
    for i, w in enumerate(windows):
        if not w["owed"]:
            continue
        for a in w["owed_list"].split(","):
            a = a.strip()
            if not a or a == "none":
                continue
            a = int(a)
            bit = 1 << a
            if any((windows[j]["got"] & bit) for j in range(i + 1, min(i + 4, len(windows)))):
                late[a] += 1
    print(f"   owed aid collected within the next 3 windows: {dict(sorted(late.items()))}")

    if acks:
        bad = sum(1 for v in acks if v != "0000")
        print(f"   acks with a failed client: {bad}/{len(acks)} ({100.0*bad/len(acks):.1f}%)")
    print()
