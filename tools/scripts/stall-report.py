#!/usr/bin/env python3
"""Frame-by-frame report on a guest that stalls during Tetris DS's start press.

    tools/scripts/stall-report.py LOG [LOG...]
    tools/scripts/stall-report.py --rows LOG [LOG...]   # one line per log

A storm (the host repeating its 264-byte start packet hundreds of times while the
guest's join-data chunk stays at 08) is the loss this project keeps hitting, and
the question this answers is *where* the stall lives: in the link, in the copy
this tree hands the console, or in the guest's own game.

It prints, per log:

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
    guest reads         the halfwords the guest's game read out of the frame it
                        was handed, split by where they sit: the first 12 bytes
                        are the RX header this tree writes, everything behind it
                        is the payload. From v0.1.9ex the value each read
                        returned is logged too, and then the header values are
                        compared against the header the delivery logged
                        (`offered to this console's game: ..., header[...]`) -
                        that comparison is the field diff: it says whether the
                        console's game is reading the copy this tree wrote.
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
OFFERED = re.compile(r"\[melonDS p(\d):Debug\] \[mp\] cmd seq=(\w+) offered to this console's game: "
                     r"rxflags=(\w+), (\d+) byte 802\.11 frame, header\[([0-9A-F ]+)\] at (\w+)")
# v0.1.9ev logged the offset only; v0.1.9ex adds `=value`.
READ = re.compile(r"\[melonDS p(\d):Debug\] \[mp\] the console's game reads the command it was handed: "
                  r"cmd seq=(\w+), (\d+) halfwords read so far, this one at (\w+)(?:=(\w+))?"
                  r" of the (\d+) byte region from (\w+)")
RX = re.compile(r"\[link-trace\] p(\d) rx (cmd|bcast).*?t=(\d+)")


def chunk_of(body):
    """`04 81 08 ...` -> '08'; anything else -> None."""
    parts = body.split()
    if len(parts) >= 3 and parts[0] == "04" and parts[1] == "81":
        return parts[2]
    return None


def parse(path):
    """Everything the report needs, from one log."""
    op = gzip.open if path.endswith(".gz") else open
    out = {"frames": [], "arms": [], "sent": [], "rx": [], "delivered": [],
           "reads": [], "body_of": {}, "offered": {}}
    pending = {}
    body_of = out["body_of"]
    with op(path, "rt", errors="replace") as f:
        for line in f:
            m = BODY.search(line)
            if m:
                pending[m.group(1)] = m.group(4).strip()
                # The trace prints `body[N]=` with N the body's length but only
                # the first 18 bytes as hex, so the length, not the text, is
                # what classifies a command. Only the host's transmissions index
                # them: the guest's own frames reuse sequence numbers.
                if m.group(2) == "1":
                    body_of[m.group(1)] = int(m.group(3))
                continue
            m = TX.search(line)
            if m:
                out["frames"].append((int(m.group(4)), pending.pop(m.group(3), "")))
                continue
            m = DELIVERED.search(line)
            if m:
                out["delivered"].append((int(m.group(1)), int(m.group(2))))
                continue
            m = OFFERED.search(line)
            if m:
                out["offered"][(int(m.group(1)), m.group(2))] = m.group(5).split()
                continue
            m = READ.search(line)
            if m:
                out["reads"].append({
                    "guest": int(m.group(1)), "seq": m.group(2),
                    "hw": int(m.group(3)), "off": int(m.group(4), 16),
                    "val": int(m.group(5), 16) if m.group(5) else None,
                    "zlen": int(m.group(6)), "zstart": int(m.group(7), 16)})
                continue
            m = ARM.search(line)
            if m:
                c = chunk_of(m.group(4))
                if c is not None:
                    out["arms"].append((int(m.group(1)), int(m.group(6)), c))
                continue
            m = SENT.search(line)
            if m:
                c = chunk_of(m.group(3))
                if c is not None:
                    out["sent"].append((int(m.group(1)), c))
                continue
            m = RX.search(line)
            if m:
                out["rx"].append((int(m.group(1)), m.group(2), int(m.group(3))))
    return out


def split_reads(reads, guest):
    """(header reads, reads past the header) for one guest.

    The log line is throttled hard (first six reads of a zone, then every 64th),
    so the *count* of lines is not a read count. What each line carries is how
    many halfwords of that zone the game had taken when it was printed, so a
    line at 3-6 halfwords is the game still inside the 12-byte header and a line
    at 64+ is the game past it, reading the payload. The offset says where in
    the zone the read landed.
    """
    hdr, deep = [], []
    for r in reads:
        if r["guest"] != guest:
            continue
        rel = (r["off"] - r["zstart"]) & 0x1FFE
        (hdr if r["hw"] <= 6 else deep).append(r)
    return hdr, deep


def header_check(hdr, offered, only_written=True):
    """Do the header values the game read match the header the delivery logged?

    `only_written` skips halfword 2, the `+4` word the hardware leaves as it
    stands (see `Wifi::CheckRX`): this tree does not write it, so a mismatch
    there is the old RAM content changing, not a field we got wrong.
    """
    checked = mismatched = 0
    for seq, zone in sorted({(r["seq"], r["zstart"]) for r in hdr}):
        h = offered.get((hdr[0]["guest"], seq))
        if not h:
            continue
        for r in hdr:
            if r["seq"] != seq or r["zstart"] != zone or r["val"] is None:
                continue
            i = ((r["off"] - r["zstart"]) & 0x1FFE) // 2
            if i >= len(h) or (only_written and i == 2):
                continue
            checked += 1
            if r["val"] != int(h[i], 16):
                mismatched += 1
    return checked, mismatched


def report(path):
    d = parse(path)
    start = [(t, b) for t, b in d["frames"] if b.startswith("06 01") and " 80 11 04 " in b]
    cont = [(t, b) for t, b in d["frames"] if b.startswith("06 01") and " 11 05 " in b]

    print(f"== {path}")
    if not start:
        print("  host start packet: never sent - the party never formed")
        return
    t0, tN = start[0][0], start[-1][0]
    print(f"  host start packet: {len(start)} times, first {t0/1e6:.3f} s, "
          f"last {tN/1e6:.3f} s, {len(cont)} `05` continues")

    per_guest = {}
    for g, clock, c in d["arms"]:
        per_guest.setdefault(g, []).append((clock, c))
    guests = sorted(per_guest)
    for g in guests:
        seqs, last = [], None
        for clock, c in per_guest.get(g, []):
            if c != last:
                seqs.append(f"{c}@{clock/1e6:.3f}s")
                last = c
        print(f"  guest p{g} chunks: {' -> '.join(seqs) if seqs else '-'}")

        reads = [r for r in d["rx"] if r[0] == g]
        after = [t for _, k, t in reads if t > t0]
        after_last = [t for _, k, t in reads if t > tN]
        kinds = {}
        for gg, k, _ in reads:
            if gg == g:
                kinds[k] = kinds.get(k, 0) + 1
        last_read = max((t for _, _, t in reads), default=None)
        print(f"  guest p{g} reads after the first start packet: {len(after)} "
              f"({', '.join(f'{k} {v}' for k, v in sorted(kinds.items())) or 'none'}), "
              f"after the last one: {len(after_last)}, "
              f"last read at {last_read/1e6:.3f} s" if last_read else
              f"  guest p{g} reads after the first start packet: {len(after)}")

        big = [b for gg, b in d["delivered"] if gg == g]
        sizes = {}
        for b in big:
            sizes[b] = sizes.get(b, 0) + 1
        print(f"  guest p{g} delivered by its own software: {sizes.get(288, 0)} x 288 byte "
              f"(the host's start packet) of {len(big)} frames, "
              f"sizes {' '.join(f'{b}:{n}' for b, n in sorted(sizes.items())[-5:])}")

        mine = [r for r in d["reads"] if r["guest"] == g]
        hdr, deep = split_reads(mine, g)
        if mine:
            print(f"  guest p{g} reads logged: {len(mine)} lines "
                  f"({len(hdr)} still inside the 12-byte RX header, {len(deep)} past it)")
            if deep:
                first = min(deep, key=lambda r: r["hw"])
                rel = (first["off"] - first["zstart"]) & 0x1FFE
                print(f"  guest p{g} first logged read past the header (any command): "
                      f"{first['hw']} halfwords of {first['zlen']} bytes taken, "
                      f"at {rel} bytes into the region (the command body starts at 36)")
            if hdr and all(r["val"] is not None for r in hdr):
                first = hdr[0]
                offered = d["offered"].get((g, first["seq"]))
                if offered:
                    print(f"  guest p{g} first command read: {' '.join(o[:4] for o in offered[:6])} "
                          f"offered, {first['val']:04X} taken at +{((first['off'] - first['zstart']) & 0x1FFE)}")
                checked, bad = header_check(hdr, d["offered"])
                print(f"  guest p{g} header fields checked against the delivery: {checked}, "
                      f"{bad} differ"
                      f"{'' if bad == 0 else ' (halfword +4 is not written by the hardware and is not counted)'}")
            elif hdr:
                print(f"  guest p{g} header reads: offsets only (this log predates the "
                      f"value being printed)")

        wrote = [c for g2, c in d["sent"] if g2 == g]
        moved = len(set(wrote)) > 1
        print(f"  guest p{g} answers sent: {len(wrote)}, "
              f"chunks written: {' '.join(sorted(set(wrote))) or '-'}"
              f"{'' if moved else '  (no second chunk written)'}")


def rows(path):
    """One line per log: the two numbers that decide whether the guest's game
    took the host's start packet, and the chunk sequence it armed."""
    d = parse(path)
    for g in sorted({a[0] for a in d["arms"]}):
        hdr, deep = split_reads(d["reads"], g)
        start_body = sum(1 for r in deep if d["body_of"].get(r["seq"]) == 264)
        roster = sum(1 for r in deep if d["body_of"].get(r["seq"]) == 242)
        seq, last = [], None
        for _, c in [(a[1], a[2]) for a in d["arms"] if a[0] == g]:
            if c != last:
                seq.append(c)
                last = c
        print(f"{path.split('/')[-1]:20s} p{g} header-reads={len(hdr):4d} "
              f"start-body-reads={start_body:3d} roster={roster:2d} "
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
