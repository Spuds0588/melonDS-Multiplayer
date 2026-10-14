#!/usr/bin/env python3
"""dp_attach_gate.py - the shared early predictor for the Download Play guest
attach lottery (SFC p65-p67, SM64 runs 4-5).

After the download boots, the guest's game either comes up on the radio or it
never does - and when it never does, both sides go quiet with no deauth: the
host stops transmitting ~10-30 s post-boot while the guest's rx freezes at a
few thousand frames, ending minutes later on the error dialogs. The decision
is visible in the app log within seconds, so no route should wait out a 100 s
setup gate on a dead session.

The rule, measured not guessed: past boot+15 s, the host's reply-window reads
AND the guest's cmds rx must both be climbing. Healthy runs climb by thousands
per 5 s window (SM64 run 1: guest rx 0 -> 208 in the first second past boot);
dead runs read +0 forever on both (SM64 run 4: rx frozen 1568, run 5: 2665;
SFC p65: host tx frozen 8864). A +50 climb over a >=10 s span separates them
with two orders of magnitude to spare.

Single-shot: read the app log as it stands and answer now.
  exit 0  attached (both climbing past boot+15 s)
  exit 1  dead (a 10 s+ span past boot+15 s with no climb on either side)
  exit 2  undecided (no boot yet, or boot+15 s not reached - poll again; a
          retry driver loops this until 0/1 or its timeout)

Requires MD_LINK_VERBOSE=1 in the session (the digest lines it reads only
exist in verbose mode). Works on a live log and on a saved one (it reads the
log's own clock, so replaying an old log answers from its end).

Usage: python3 tools/scripts/dp_attach_gate.py logs/sm64_app.log
"""

import re
import sys
from datetime import datetime

BOOT_RE = re.compile(r"booted the downloaded image|the download is complete")
TS_RE = re.compile(r"\[(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})")
GUEST_RE = re.compile(r"mp p2 \[guest\]: cmds rx (\d+)")
HOST_RE = re.compile(r"mp p1 \[host\]:.*window reads (\d+)")


def parse_ts(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%S")


def main(path):
    try:
        with open(path, errors="replace") as f:
            lines = f.readlines()
    except OSError as e:
        print(f"UNDECIDED: cannot read {path}: {e}")
        return 2

    boot_at = None
    for line in lines:
        m = TS_RE.search(line)
        if m and BOOT_RE.search(line):
            boot_at = parse_ts(m.group(1))
    if boot_at is None:
        print(f"UNDECIDED: no boot in {path} yet")
        return 2

    g = []  # (ts, cmds rx) samples past boot+15 s
    h = []  # (ts, window reads) samples past boot+15 s
    for line in lines:
        m = TS_RE.search(line)
        if not m:
            continue
        ts = parse_ts(m.group(1))
        if (ts - boot_at).total_seconds() < 15:
            continue
        gm = GUEST_RE.search(line)
        if gm:
            g.append((ts, int(gm.group(1))))
        hm = HOST_RE.search(line)
        if hm:
            h.append((ts, int(hm.group(1))))

    if not g or not h:
        print(f"UNDECIDED: boot at {boot_at}, no digest lines past boot+15 s yet "
              f"(needs MD_LINK_VERBOSE=1)")
        return 2
    # The verdict reads the RECENT span (last ~15 s of the log), not first to
    # last: a session that attached and died late must read DEAD, not ride its
    # early climb. In health the recent span always climbs by thousands.
    end = max(g[-1][0], h[-1][0])
    gb = next((s for s in g if (end - s[0]).total_seconds() <= 15), g[0])
    hb = next((s for s in h if (end - s[0]).total_seconds() <= 15), h[0])
    gspan = (g[-1][0] - gb[0]).total_seconds()
    hspan = (h[-1][0] - hb[0]).total_seconds()
    if gspan < 10 or hspan < 10:
        print(f"UNDECIDED: boot at {boot_at}, recent span only {gspan:.0f}s/"
              f"{hspan:.0f}s (need 10 s past boot+15 s)")
        return 2

    gclimb = g[-1][1] - gb[1]
    hclimb = h[-1][1] - hb[1]
    verdict = "ATTACHED" if (gclimb > 50 and hclimb > 50) else "DEAD"
    print(f"{verdict}: boot {boot_at}, recent {gspan:.0f}s: "
          f"guest rx {gb[1]} -> {g[-1][1]} (+{gclimb}), "
          f"host reads {hb[1]} -> {h[-1][1]} (+{hclimb})")
    return 0 if verdict == "ATTACHED" else 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <app-log>", file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
