#!/usr/bin/env python3
"""Fingerprint the start of a Tetris DS room from a verbose link log.

    tools/scripts/mp-handshake.py logs/t2t1.log logs/t2t2.log ...

Reads only what the link trace already prints with `MD_LINK_VERBOSE=1`, one row
per log. Every column answers a question the 2026-10-23 trials needed answered
per run rather than once per pass:

    out      PLAY  the room reached a match body (`F6 00 .. 00 04/05/06 9D`)
             STORM the host repeated its 264-byte start packet more than 20 times
                   and never sent a room body afterwards
             CLEAN the room body came back but no match body followed
             NO-ROOM no start packet at all: the party never formed
    hs       how many times the host sent the start packet
             `06 01 <mask> 00 80 11 04 00 00 00 00 4E 49 4E 54 45 4E 44` (264 B)
    cont     how many times it sent the `05` continue body after that
    t_hs     when the first start packet went out, emulated seconds
    t_room   when the room body (`F6 00 .. 00 00 80`) came back, `-` if never
    t_0000   when the host's own mask fell to 0000 (`06 01 00 00 00 00`)
    chunk    each guest's last join-data chunk (`04 81 <chunk> ..`) before the
             start packet, then after it: `08>0B` means the guest was at chunk
             08 when the handshake began and finished at 0B. The trials showed
             this is the variable that separates the two outcomes - a guest that
             stays at `08` while the handshake asks stalls the host (a storm),
             one that moves past it lets the room through. Where it lands differs
             between runs: 0B in most wins, 0A in two (t2v1, t2w1).
    armed    guest answers that carried data the guest's game wrote, against
             answers that fell back to "nothing armed: the firmware's own reply"
             (the link delivers both the same way: collected == answered in every
             trial, so this is the games, not the wire).

Colon-free output, one row per log, so two arms can be diffed with `sort`/`awk`.
"""
import gzip
import re
import sys

BODY = re.compile(r"\[link-trace\] cmd body seq=([0-9A-F]+) slot=1 body\[(\d+)\]=([0-9A-F ]+)")
TX = re.compile(r"\[link-trace\] p1 tx cmd\s+len=\s*(\d+) chan=\s*\d+ fc=(\w+) seq=([0-9A-F]+).*?t=(\d+)")
ARM = re.compile(r"\[melonDS (p\d):Debug\] \[mp\] answering cmd seq=\S+: .*?my clock (\d+), (.*)$")
CHUNK = re.compile(r"\[melonDS (p\d):Debug\] \[mp\] guest arms an answer: .*?body\[(\d+)\]=(04 81 ([0-9A-F]{2}) )")
CLOCK = re.compile(r"my clock (\d+)")
PLAY = re.compile(r"body\[[0-9]+\]=F6 00 [0-9A-F]{2} 00 0[4-6] 9D")
ROOM = "00 80"


def fingerprint(path):
    pending, frames, answers, chunks = {}, [], [], []
    plays = 0
    op = gzip.open if path.endswith(".gz") else open
    with op(path, "rt", errors="replace") as f:
        for line in f:
            m = BODY.search(line)
            if m:
                pending[m.group(1)] = m.group(3).strip()
                continue
            m = TX.search(line)
            if m:
                frames.append((int(m.group(4)), pending.pop(m.group(3), "")))
                continue
            m = ARM.search(line)
            if m:
                answers.append((int(m.group(2)), "nothing armed" in m.group(3)))
                continue
            m = CHUNK.search(line)
            if m:
                c = CLOCK.search(line)
                if c:
                    chunks.append((int(c.group(1)), m.group(1), m.group(4)))
                continue
            if PLAY.search(line):
                plays += 1

    hs = [t for t, b in frames if b.startswith("06 01") and " 80 11 04 " in b]
    cont = [t for t, b in frames if b.startswith("06 01") and " 11 05 " in b]
    room = [t for t, b in frames if b.startswith("F6 00") and b.endswith(ROOM)
            and (not hs or t >= hs[0])]
    zero = [t for t, b in frames if b.startswith("06 01 00 00")]
    if not hs:
        out = "NO-ROOM"
    elif plays:
        out = "PLAY"
    elif len(hs) > 20 and not room:
        out = "STORM"
    elif room:
        out = "CLEAN"
    else:
        out = "NO-ROOM"
    t_hs = hs[0] if hs else None
    after = {}
    for t, p, ch in chunks:
        if t_hs is None or t > t_hs:
            after[p] = ch
    before = {}
    for t, p, ch in chunks:
        if t_hs is not None and t <= t_hs:
            before[p] = ch
    chunk = ",".join(f"{p}:{before.get(p, '-')}>{after.get(p, '-')}"
                     for p in sorted(set(before) | set(after))) or "-"
    armed = sum(1 for _, nothing in answers if not nothing)
    blank = sum(1 for _, nothing in answers if nothing)

    def s(us):
        return f"{us/1e6:.3f}" if us else "-"

    return (f"{path.split('/')[-1]:20s} out={out:7s} hs={len(hs):4d} cont={len(cont):4d} "
            f"t_hs={s(t_hs):>7s} t_room={s(room[0] if room else None):>7s} "
            f"t_0000={s(zero[0] if zero else None):>7s} chunk={chunk:14s} "
            f"armed={armed}/{armed + blank}")


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().split("\n\n")[0], file=sys.stderr)
        return 2
    for path in sys.argv[1:]:
        print(fingerprint(path), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
