#!/usr/bin/env python3
"""Frame-by-frame report on a guest that stalls during Tetris DS's start press.

    tools/scripts/stall-report.py LOG [LOG...]
    tools/scripts/stall-report.py --rows LOG [LOG...]   # one line per log

A storm (the host repeating its 264-byte start packet hundreds of times while the
guest's join-data chunk stays at 08) is the loss this project keeps hitting, and
the question this answers is *where* the stall lives: in the link, or in the
guest's own game. It prints, per log:

    host start packet   first / last time the host sent
                        `06 01 <mask> 00 80 11 04 ...` (264 B), and how many times
    host continues      how many `05` bodies followed the first of them
    guest chunks        every join-data chunk the guest's game armed (`04 81 XX`),
                        with the guest's own emulated clock, so a chunk that
                        never moves is visible as a gap, not inferred
    guest reads after   frames the guest's game *received* after the first start
                        packet (`rx cmd`, `rx bcast`), and after the last one -
                        a guest still reading is a guest whose radio works
    guest delivered     frames handed to the guest's own software, by size: a
                        288 byte frame is the host's 264 byte start packet plus
                        its 24 byte 802.11 header, so this says whether the
                        packet the host is repeating ever reached the game
    guest reads         command halfwords the guest's game read, split into
                        header-only (3-6 halfwords: the game looks and moves on)
                        and body (64/128 halfwords: the game read the payload)
    guest answers after answers its game armed / sent after the first start packet

Reads only what `MD_LINK_VERBOSE=1` already prints. No emulator needed.
"""
import gzip
import re
import sys

BODY = re.compile(r"\[link-trace\] cmd body seq=([0-9A-F]+) slot=(\d) body\[(\d+)\]=([0-9A-F ]+)")
TX = re.compile(r"\[link-trace\] p1 tx cmd\s+len=\s*(\d+) chan=\s*\d+ fc=(\w+) seq=([0-9A-F]+).*?t=(\d+)")
ARM = re.compile(r"\[melonDS p(\d):Debug\] \[mp\] guest arms an answer: arm #(\d+), .*?body\[(\d+)\]=([0-9A-F ]+)"
                 r" \(last cmd seq ([0-9A-F]+), my clock (\d+)\)")
SENT = re.compile(r"\[melonDS p(\d):Debug\] \[mp\] answer sent: .*?body\[(\d+)\]=([0-9A-F ]+)")
DELIVERED = re.compile(r"\[melonDS p(\d):Debug\] \[mp\] packet delivered to the console's own software: "
                       r"\d+ so far, (\d+) byte frame")
READ = re.compile(r"\[melonDS p(\d):Debug\] \[mp\] the console's game reads the command it was handed: "
                  r"cmd seq=(\w+), (\d+) halfwords read so far")
RX = re.compile(r"\[link-trace\] p(\d) rx (cmd|bcast).*?t=(\d+)")


def chunk_of(body):
    """`04 81 08 ...` -> '08'; anything else -> None."""
    parts = body.split()
    if len(parts) >= 3 and parts[0] == "04" and parts[1] == "81":
        return parts[2]
    return None


def report(path):
    op = gzip.open if path.endswith(".gz") else open
    pending = {}
    frames = []          # (t, body) host transmissions
    arms = []            # (guest, clock_us, chunk)
    sent = []            # (guest, chunk)
    rx = []              # (guest, kind, t)
    delivered = []       # (guest, bytes)
    reads = []           # (guest, halfwords, seq)
    body_of = {}         # seq -> body, for every command the host sent
    with op(path, "rt", errors="replace") as f:
        for line in f:
            m = BODY.search(line)
            if m:
                pending[m.group(1)] = m.group(4).strip()
                # The trace prints `body[N]=` with N the body's length but only
                # the first 18 bytes as hex, so the length, not the text, is
                # what classifies a command.
                if m.group(2) == "1":
                    body_of[m.group(1)] = int(m.group(3))
                continue
            m = TX.search(line)
            if m:
                frames.append((int(m.group(4)), pending.pop(m.group(3), "")))
                continue
            m = DELIVERED.search(line)
            if m:
                delivered.append((int(m.group(1)), int(m.group(2))))
                continue
            m = READ.search(line)
            if m:
                reads.append((int(m.group(1)), int(m.group(3)), m.group(2)))
                continue
            m = ARM.search(line)
            if m:
                c = chunk_of(m.group(4))
                if c is not None:
                    arms.append((int(m.group(1)), int(m.group(6)), c))
                continue
            m = SENT.search(line)
            if m:
                c = chunk_of(m.group(3))
                if c is not None:
                    sent.append((int(m.group(1)), c))
                continue
            m = RX.search(line)
            if m:
                rx.append((int(m.group(1)), m.group(2), int(m.group(3))))

    start = [(t, b) for t, b in frames
             if b.startswith("06 01") and " 80 11 04 " in b]
    cont = [(t, b) for t, b in frames if b.startswith("06 01") and " 11 05 " in b]

    print(f"== {path}")
    if not start:
        print("  host start packet: never sent - the party never formed")
        return
    t0, tN = start[0][0], start[-1][0]
    print(f"  host start packet: {len(start)} times, first {t0/1e6:.3f} s, "
          f"last {tN/1e6:.3f} s, {cont and len(cont) or 0} `05` continues")

    per_guest = {}
    for g, clock, c in arms:
        per_guest.setdefault(g, []).append((clock, c))
    for g in sorted(per_guest):
        seq, last = [], None
        for clock, c in per_guest[g]:
            if c != last:
                seq.append(f"{c}@{clock/1e6:.3f}s")
                last = c
        print(f"  guest p{g} chunks: {' -> '.join(seq) if seq else '-'}"
              f"  ({len(per_guest[g])} answers)")
        after = [t for gg, k, t in rx if gg == g and t > t0]
        after_last = [t for gg, k, t in rx if gg == g and t > tN]
        kinds = {}
        for gg, k, t in rx:
            if gg == g and t > t0:
                kinds[k] = kinds.get(k, 0) + 1
        last_read = max((t for gg, k, t in rx if gg == g), default=None)
        print(f"  guest p{g} reads after the first start packet: {len(after)} "
              f"({', '.join(f'{k} {v}' for k, v in sorted(kinds.items())) or 'none'}), "
              f"after the last one: {len(after_last)}, "
              f"last read at {last_read/1e6:.3f} s" if last_read else
              f"  guest p{g} reads after the first start packet: {len(after)}")
        wrote = [c for g2, c in sent if g2 == g]
        moved = len({c for c in wrote}) > 1
        print(f"  guest p{g} answers sent after the press: {len(wrote)}, "
              f"chunks written: {' '.join(sorted(set(wrote))) or '-'}"
              f"{'' if moved else '  (no chunk written after the press)'}")
        big = [b for gg, b in delivered if gg == g]
        sizes = {}
        for b in big:
            sizes[b] = sizes.get(b, 0) + 1
        start_packets = sizes.get(288, 0)
        print(f"  guest p{g} delivered by its own software: "
              f"{start_packets} x 288 byte (the host's start packet) of {len(big)} frames, "
              f"sizes {' '.join(f'{b}:{n}' for b, n in sorted(sizes.items())[-5:])}")
        rd = [h for gg, h, _ in reads if gg == g]
        header = sum(1 for h in rd if h <= 6)
        body = sum(1 for h in rd if h >= 64)
        print(f"  guest p{g} command reads: {len(rd)} ({header} header-only, {body} read the body)")
        # Which commands' bodies did the guest's game actually read? A payload it
        # reads is a payload it accepted; a start packet it only header-reads is
        # a start packet it left in the buffer.
        kinds = {}
        for gg, h, seq in reads:
            if gg != g or h < 64:
                continue
            n = body_of.get(seq)
            if n == 264:
                kinds["start packet (264 B)"] = kinds.get("start packet (264 B)", 0) + 1
            elif n == 242:
                kinds["roster (242 B)"] = kinds.get("roster (242 B)", 0) + 1
            else:
                kinds[f"other ({n} B)"] = kinds.get(f"other ({n} B)", 0) + 1
        print(f"  guest p{g} read the body of: "
              f"{', '.join(f'{k} x{v}' for k, v in sorted(kinds.items())) or 'nothing'}")


def rows(path):
    """One line per log: the two numbers that decide whether the guest's game
    took the host's start packet, and the chunk sequence it armed."""
    op = gzip.open if path.endswith(".gz") else open
    body_of, reads, arms = {}, [], []
    with op(path, "rt", errors="replace") as f:
        for line in f:
            m = BODY.search(line)
            if m:
                if m.group(2) == "1":
                    body_of[m.group(1)] = int(m.group(3))
                continue
            m = READ.search(line)
            if m and int(m.group(1)) == 2:
                reads.append((int(m.group(3)), m.group(2)))
                continue
            m = ARM.search(line)
            if m and int(m.group(1)) == 2:
                c = chunk_of(m.group(4))
                if c:
                    arms.append((int(m.group(6)), c))
    start = sum(1 for h, s in reads if h >= 64 and body_of.get(s) == 264)
    roster = sum(1 for h, s in reads if h >= 64 and body_of.get(s) == 242)
    seq, last = [], None
    for _, c in arms:
        if c != last:
            seq.append(c)
            last = c
    print(f"{path.split('/')[-1]:20s} start-body-reads={start:2d} roster={roster:2d} "
          f"chunks={'->'.join(seq) or '-'}")


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__.strip().splitlines()[2].strip())
    if args[0] == "--rows":
        for path in args[1:]:
            rows(path)
        return 0
    for path in args:
        report(path)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
