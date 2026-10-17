#!/usr/bin/env python3
"""Per-console poll-offer rates and association events out of one app log.

Why this exists: the question "does the link still hold three associated clients,
or does the second association end the first" cannot be answered from the end of
a run (every console's digest prints `+0` once it has stopped). It is answered by
*when* each console's own poll stream stops, which needs the log read in
10-emulated-second bins.

The bins come from each console's own clock probe (`... frames N ...`), so the
columns are comparable across consoles even though the log interleaves them.

    tools/scripts/mp-offer-rate.py logs/m4_tetris3.log.gz
    tools/scripts/mp-offer-rate.py logs/m4_tetris3.log.gz --step 300
"""
import gzip
import re
import sys
from collections import defaultdict

path = sys.argv[1]
step = 600
if "--step" in sys.argv:
    step = int(sys.argv[sys.argv.index("--step") + 1])

frame_re = re.compile(r"\[melonDS p(\d):\w+\] \[mp\] clock probe: .* frames (\d+) ")
offer_re = re.compile(r"\[melonDS p(\d):\w+\] \[mp\] cmd seq=([0-9A-F]+) offered")
aid_re = re.compile(r"\[melonDS p(\d):\w+\] \[CLIENT ([0-9A-F])] host sync=")
deauth_re = re.compile(r"\[melonDS p(\d):\w+\] \[mp\] deauth")
setaid_re = re.compile(r"\[melonDS p(\d):\w+\] \[mp\] guest set its AID to (\d+)")
credit_re = re.compile(r"\[melonDS p(\d):\w+\] \[mp\] handing client (\d)('s)? reply")
assoc = defaultdict(list)
deauths = defaultdict(list)
frame = defaultdict(int)
bins = defaultdict(lambda: defaultdict(int))
aidset = defaultdict(list)          # per console: (frame, aid) writes of its own AID
aid_bins = defaultdict(lambda: defaultdict(int))  # per console: aid-value counts per bin
credits = defaultdict(lambda: defaultdict(int))   # host: credits per aid per bin

def open_log(p):
    """Plain or gzipped: runs are gzipped when they are archived, not while live."""
    if p.endswith(".gz"):
        return gzip.open(p, "rt", errors="replace")
    import io

    return io.open(p, "r", errors="replace")


with open_log(path) as fh:
    for line in fh:
        m = frame_re.search(line)
        if m:
            p = int(m.group(1))
            n = int(m.group(2))
            if n > frame[p]:
                frame[p] = n
            continue
        m = aid_re.search(line)
        if m:
            assoc[int(m.group(1))].append((frame[int(m.group(1))], m.group(2)))
            continue
        if deauth_re.search(line):
            p = int(deauth_re.search(line).group(1))
            deauths[p].append(frame[p])
            continue
        m = credit_re.search(line)
        if m:
            credits[int(m.group(1))][(int(m.group(2)), frame[int(m.group(1))] // step)] += 1
            continue
        m = offer_re.search(line)
        if m:
            p = int(m.group(1))
            bins[p][frame[p] // step] += 1
            continue
        m = setaid_re.search(line)
        if m:
            p = int(m.group(1))
            aid = int(m.group(2))
            aidset[p].append((frame[p], aid))
            aid_bins[p][(frame[p] // step, aid)] += 1

print(f"{path}: {step}-frame bins, offered polls per console, association and deauth frames")
print("console | assoc@frame(AID) | deauth@frame | polls per bin")
for p in sorted(set(list(frame) + list(bins))):
    bins_p = bins.get(p, {})
    where = []
    for b in sorted(bins_p):
        where.append(f"{b * step}dw {bins_p[b]}")
    print(
        f"p{p} | {assoc.get(p, '-')} | {deauths.get(p, '-')} | "
        + ("  ".join(where[:24]) if where else "no polls offered")
    )

print()
print("the AID each console's own game wrote (its association identity, over time)")
for p in sorted(aid_bins):
    trans = []
    for (b, aid) in sorted(aid_bins[p]):
        trans.append(f"{b * step}dw aid{aid} x{aid_bins[p][(b, aid)]}")
    values = sorted({aid for (_, aid) in aid_bins[p]})
    print(f"p{p}: values written {values} | {trans[:16]}")

print()
print("host credits per client AID per bin (the host's own 'handing client N's reply' lines)")
for h, per in sorted(credits.items()):
    aids = sorted({aid for (aid, _) in per})
    total = sum(per.values())
    print(f"p{h} credited aid(s) {aids}, {total} hand-overs")
    for aid in aids:
        series = []
        for (a, b) in sorted((a, b) for (a, b) in per if a == aid):
            series.append(f"{b * step}dw {per[(a, b)]}")
        print(f"   aid {aid}: " + "  ".join(series[:20]))
