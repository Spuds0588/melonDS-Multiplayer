#!/usr/bin/env python3
"""Read a verbose (`MD_LINK_VERBOSE=1`) app log as a per-client table.

The 2026-10-22 pass had to answer "why does the second guest never leave
`Accepting players now`" from 30,000-line logs, one grep at a time. The lines
that matter are already there - every command offered to a console's game
(with its mask and AID), every answer that console armed and sent, and every
reply the host collected (with the aid it was filed under and whether it was
real or the firmware's stand-in `fc=0158`) - so this reads them into the table
the question actually needs:

  * each guest's association AID (`[CLIENT n] host sync=`),
  * how many polls each guest saw, how many were addressed to it, and how many
    it answered - so "the link lost frames" and "the game did not answer" are
    different columns,
  * each answer's body shape (first four bytes, with the volatile tail cut) so
    one guest's `04 81 07 01` name fragment and another's `00 80 0A 01` generic
    answer are visible as different rows,
  * and, per aid, what the host collected: how many were real (`fc=0118`) and
    how many were the firmware answering for a game that had not armed a reply
    (`fc=0158`).

    tools/scripts/accept-diff.py logs/tetris3-accept.log
    tools/scripts/accept-diff.py logs/tetris2-accept.log --compare logs/tetris3-accept.log

`--compare` prints the two tables side by side under the same headings, which is
how the two-console control and the three-console failure are meant to be read.
Exit status is non-zero when a file has no multiplayer lines at all, so a run
that never went near the link cannot be mistaken for an empty one.
"""

import argparse
import collections
import re
import sys

RE_CLIENT = re.compile(r"\[CLIENT ([0-9A-F])\] host sync=([0-9A-F]+)")
RE_CMD = re.compile(r"\[mp\] cmd seq=([0-9A-F]{4}) offered.*?mask ([0-9A-F]{4}), AID (\d+).*?(this poll is ours|this poll is NOT ours)")
RE_ARM = re.compile(r"\[mp\] answer sent: frame seq=([0-9A-F]{4}).*?(\d+) byte frame, body\[\d+\]=([0-9A-F ]+)")
RE_COLLECT = re.compile(r"reply window: p(\d+) collected a reply from p(\d+) for aid (\d+), (\d+) byte frame, fc=([0-9A-F]{4}).*?body\[\d+\]=([0-9A-F ]+)")
RE_OFFER = re.compile(r"\[mp\] cmd seq=([0-9A-F]{4}) offered")


def shape(body):
    """The stable part of an answer body: its first four bytes.

    The tails carry counters and per-round values (`5A 5A B8 B6` and the two
    bytes before it move every round), so a table keyed on the whole body would
    have a row per round and say nothing.
    """
    toks = body.split()
    return " ".join(toks[:4]) if toks else "-"


def read(path):
    who = path.split("/")[-1].replace("-accept.log", "").replace(".log", "")
    cli = {}
    ours = collections.Counter()
    notours = collections.Counter()
    cmds = collections.Counter()
    answers = collections.defaultdict(collections.Counter)
    collected = collections.defaultdict(collections.Counter)   # (sender, aid) -> shape
    fc = collections.defaultdict(collections.Counter)          # (sender, aid) -> fc
    masks = collections.Counter()
    hosts = collections.Counter()
    lines = 0
    for line in open(path, errors="replace"):
        lines += 1
        m = re.match(r"\[melonDS p(\d):", line)
        if not m:
            # A host-side line is still about one console; keep the ones with a prefix.
            m = re.match(r"\[2026.*mp p(\d) \[", line)
        console = m.group(1) if m else "1"
        mc = RE_CLIENT.search(line)
        if mc:
            cli[console] = (int(mc.group(1), 16), mc.group(2))
            continue
        mo = RE_CMD.search(line)
        if mo:
            cmds[console] += 1
            masks[mo.group(2)] += 1
            if mo.group(4).endswith("NOT ours"):
                notours[console] += 1
            else:
                ours[console] += 1
            continue
        if RE_OFFER.search(line):
            cmds[console] += 1
            continue
        ma = RE_ARM.search(line)
        if ma:
            answers[console][shape(ma.group(3))] += 1
            continue
        mc2 = RE_COLLECT.search(line)
        if mc2:
            sender, aid = mc2.group(2), int(mc2.group(3))
            collected[(sender, aid)][shape(mc2.group(6))] += 1
            fc[(sender, aid)][mc2.group(5)] += 1
    return {
        "name": who, "lines": lines, "cli": cli, "cmds": cmds, "ours": ours,
        "notours": notours, "answers": answers, "collected": collected,
        "fc": fc, "masks": masks,
    }


def show(t):
    print(f"=== {t['name']} ({t['lines']} lines)")
    if not t["cmds"]:
        print("  no multiplayer command lines - MD_LINK_VERBOSE was not on, or this "
              "session never reached the link")
        return False
    for c in sorted(t["cli"], key=int):
        aid, sync = t["cli"][c]
        print(f"  p{c}: associated AID {aid} (host sync {sync})")
    for c in sorted(t["cmds"], key=int):
        print(f"  p{c}: {t['cmds'][c]} polls seen, {t['ours'][c]} addressed to it, "
              f"{t['notours'][c]} not; {sum(t['answers'][c].values())} answers sent")
        for body, n in t["answers"][c].most_common(4):
            print(f"       answer body {body:<12} x{n}")
    if t["masks"]:
        top = ", ".join(f"{m}x{n}" for m, n in t["masks"].most_common(4))
        print(f"  host poll masks: {top}")
    for (sender, aid), shapes in sorted(t["collected"].items()):
        total = sum(shapes.values())
        real = t["fc"][(sender, aid)].get("0118", 0)
        canned = t["fc"][(sender, aid)].get("0158", 0)
        print(f"  host collected from p{sender} for aid {aid}: {total} "
              f"({real} real fc=0118, {canned} firmware fc=0158)")
        for body, n in shapes.most_common(3):
            print(f"       body {body:<12} x{n}")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("log")
    ap.add_argument("--compare", help="a second log to print under the same headings")
    args = ap.parse_args()

    ok = show(read(args.log))
    if args.compare:
        ok = show(read(args.compare)) and ok
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
