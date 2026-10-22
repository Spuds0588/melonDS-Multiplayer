#!/usr/bin/env python3
"""Read a verbose app log as rounds of the host's own poll-and-reply handshake.

The app's link trace (MD_LINK_VERBOSE=1) prints every step of a round already -
the host's command with its client mask, each client's delivery of it, each
client's answer or blank, and the host's reply window crediting answers by aid -
but it prints them as a stream, and the questions that decide whether a room
lives are about *rounds*:

  * did the poll that asked N clients get N answers, in that poll's own window?
  * was an answer the window credited actually made for the poll it was credited
    to, or for an earlier one?
  * how many rounds did a client answer at all, and how many with a blank?

This reads one such log and prints those counts. It changes nothing and needs no
app: give it the log `tools/run_app.sh` wrote.

    tools/scripts/round-timing.py logs/x_app.log            # the whole run
    tools/scripts/round-timing.py logs/x_app.log.gz --tail 1200

`--tail N` keeps the last N polls (a long run is mostly parked; the walk is the
interesting part).

Exit 0 when it found rounds to report, 2 when the log has no link trace (a run
without MD_LINK_VERBOSE=1 cannot answer any of this, and saying so is the
answer).
"""

import argparse
import gzip
import re
import statistics
import sys


def open_log(path):
    return gzip.open(path, "rt", errors="replace") if path.endswith(".gz") else open(path, errors="replace")


RE_POLL = re.compile(r"\[link-trace\] p(\d+) tx cmd\s+len=\s*(\d+) .*seq=([0-9A-Fa-f]+) .*mask=([0-9A-Fa-f]+)")
RE_OFFERED = re.compile(r"\[mp\] cmd seq=([0-9A-Fa-f]+) offered to this console's game: .*mask ([0-9A-Fa-f]+)")
RE_ANSWERING = re.compile(r"\[mp\] answering cmd seq=([0-9A-Fa-f]+): AID (\d+)")
RE_BLANK = re.compile(r"\[mp\] BLANK reply to cmd seq=([0-9A-Fa-f]+)")
RE_SENT = re.compile(r"\[mp\] answer sent: frame seq=([0-9A-Fa-f]+) arm=#\d+ armed in round seq ([0-9A-Fa-f]+)")
RE_COLLECTED = re.compile(r"reply window: p(\d+) collected a reply from p(\d+) for aid (\d+), .*seq=([0-9A-Fa-f]+)")
RE_DONE = re.compile(r"reply window: p(\d+) has everyone it asked for \(collected=([0-9A-Fa-f]+) of mask ([0-9A-Fa-f]+)\)")
RE_CLOSED = re.compile(r"reply window closed for p(\d+) with collected=([0-9A-Fa-f]+)")
RE_UNREAD = re.compile(r"reply window: starting a new command for p(\d+) with (\d+) unread")
RE_EVENT = re.compile(r"\[link-event\] \+([\d.]+)s wall frame (\d+) p(\d+) \(player(\d+)\): (.*)")
RE_HW = re.compile(r"mp p(\d) hw: (.*)")
RE_IRQ = re.compile(r"mp p(\d) irq: (.*)")
RE_TAG = re.compile(r"melonDS p(\d+)")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--tail", type=int, default=0, help="keep only the last N polls")
    args = ap.parse_args()

    reply_to_poll = {}   # (client, reply frame seq) -> poll seq it answered
    answering = {}       # client -> poll seq of its last `answering` line
    polls = []
    windows = {}
    offered, blanks = {}, {}
    events, hw_last, irq_last = [], {}, {}
    cur = None

    with open_log(args.log) as fh:
        for ln in fh:
            if "[link-trace] p" in ln:
                m = RE_POLL.search(ln)
                if m and m.group(1) == "1":
                    cur = m.group(3)
                    polls.append({"seq": cur, "mask": int(m.group(4), 16)})
                    windows[cur] = {"collected": 0, "aids": set(), "done": None, "replies": []}
                continue
            if "reply window" in ln:
                if cur is None:
                    continue
                w = windows[cur]
                m = RE_COLLECTED.search(ln)
                if m:
                    w["collected"] += 1
                    w["aids"].add(int(m.group(3)))
                    w["replies"].append((int(m.group(2)), m.group(4)))
                    continue
                m = RE_DONE.search(ln)
                if m:
                    w["done"] = int(m.group(2), 16)
                    continue
                m = RE_CLOSED.search(ln)
                if m:
                    w["done"] = int(m.group(2), 16)
                    continue
                if RE_UNREAD.search(ln):
                    w["reanchor"] = True
                continue
            if "[mp] " in ln:
                tag = RE_TAG.search(ln)
                client = int(tag.group(1)) if tag else None
                m = RE_ANSWERING.search(ln)
                if m and client is not None:
                    answering[client] = m.group(1)
                    continue
                m = RE_SENT.search(ln)
                if m and client is not None:
                    reply_to_poll[(client, m.group(1))] = answering.get(client)
                    continue
                m = RE_BLANK.search(ln)
                if m and client is not None:
                    blanks.setdefault(client, {})[m.group(1)] = 1
                    continue
                m = RE_OFFERED.search(ln)
                if m and client is not None:
                    offered.setdefault(client, {})[m.group(1)] = int(m.group(2), 16)
                    continue
                continue
            m = RE_EVENT.search(ln)
            if m:
                events.append((float(m.group(1)), int(m.group(2)), int(m.group(4)), m.group(5)))
                continue
            m = RE_HW.search(ln)
            if m:
                hw_last[int(m.group(1))] = m.group(2)
                continue
            m = RE_IRQ.search(ln)
            if m:
                irq_last[int(m.group(1))] = m.group(2)

    if not polls:
        print("no host poll in this log: run the app with MD_LINK_VERBOSE=1 "
              "(tools/run_app.sh does) and this can read it", file=sys.stderr)
        return 2

    order = {q["seq"]: i for i, q in enumerate(polls)}
    for p in polls:
        w = windows[p["seq"]]
        p["collected"] = w["collected"]
        p["aids"] = w["aids"]
        p["done"] = w["done"]
        p["lags"] = []
        p["unasked"] = 0
        for client, replyseq in w["replies"]:
            answered = reply_to_poll.get((client, replyseq))
            if answered in order:
                p["lags"].append(order[p["seq"]] - order[answered])
            else:
                p["lags"].append(None)
            if p["mask"] and not (p["mask"] & (1 << (client - 1))):
                p["unasked"] += 1

    if args.tail:
        polls = polls[-args.tail:]

    print(f"{len(polls)} host polls read from {args.log}")
    masks = {}
    for p in polls:
        masks[p["mask"]] = masks.get(p["mask"], 0) + 1
    print("poll masks asked: " + ", ".join(f"{m:#06x} x{n}" for m, n in sorted(masks.items())))
    for p in polls:
        p["aidmask"] = 0
        for a in p["aids"]:
            p["aidmask"] |= 1 << a
    full = [p for p in polls if p["mask"] and (p["aidmask"] & p["mask"]) == p["mask"]]
    none = [p for p in polls if not p["aids"]]
    print(f"windows that credited every client asked for: {len(full)}/{len(polls)}")
    print(f"windows that credited nobody: {len(none)}/{len(polls)}")
    print(f"windows that closed with no answer at all: "
          f"{sum(1 for p in polls if p['done'] == 0)}")
    lags = [g for p in polls for g in p["lags"]]
    print("how many polls back the credited answer was made for: "
          + ", ".join(f"{k} poll(s): {sum(1 for g in lags if g == k)}"
                      for k in (0, 1, 2) if any(g == k for g in lags))
          + f", 3 or more: {sum(1 for g in lags if g is not None and g >= 3)},"
          + f" unknown: {sum(1 for g in lags if g is None)}")
    print(f"replies credited from a client the poll did not ask: "
          f"{sum(p['unasked'] for p in polls)}")
    print(f"answers per window: median {statistics.median(p['collected'] for p in polls):.0f}, "
          f"max {max(p['collected'] for p in polls)}")

    # A run has phases (before the join, the join, the walk, whatever follows),
    # and the whole-run average hides which of them the misses are in - so the
    # same numbers again per block of polls, small enough to point at a moment.
    block = max(50, len(polls) // 12)
    print(f"\nper {block} polls (asked / credited-all / credited-none / lag1 / unasked):")
    for i in range(0, len(polls), block):
        chunk = polls[i:i + block]
        asked = statistics.median(p["mask"].bit_count() for p in chunk)
        allc = sum(1 for p in chunk if p["mask"] and (p["aidmask"] & p["mask"]) == p["mask"])
        none = sum(1 for p in chunk if not p["aids"])
        lag1 = sum(1 for p in chunk for g in p["lags"] if g == 1)
        unasked = sum(p["unasked"] for p in chunk)
        print(f"  polls {i + 1:5d}-{i + len(chunk):5d}: asked {asked:.0f}  {allc:4d}  {none:4d}  "
              f"{lag1:4d}  {unasked:3d}")

    pollset = {p["seq"] for p in polls}
    print("\nper client, over the same polls:")
    for c in sorted(set(list(offered) + list(blanks))):
        got = [s for s in offered.get(c, {}) if s in pollset]
        blank = [s for s in blanks.get(c, {}) if s in pollset]
        print(f"  p{c}: saw {len(got)} of {len(polls)} polls, blank on {len(blank)} "
              f"({100.0 * len(blank) / max(len(got), 1):.0f}% of what it saw)")

    if events:
        print("\nlink events (radio on/off, deauth, resync), last twelve:")
        for t, frame, player, what in events[-12:]:
            print(f"  +{t:8.2f}s frame {frame:6d} p{player}: {what[:110]}")
        print("\nthe wireless block as the last digest read it:")
        for p in sorted(hw_last):
            print(f"  p{p} hw: {hw_last[p][:160]}")
        for p in sorted(irq_last):
            print(f"  p{p} irq: {irq_last[p][:160]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
