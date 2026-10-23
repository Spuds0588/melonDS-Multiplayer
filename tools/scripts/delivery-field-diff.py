#!/usr/bin/env python3
"""Field diff of what a guest's console was handed, between two melonDS logs.

The question this answers is a narrow one and it is asked of the *copy*, not of
a hypothesis about the guest's game: when the host retransmits a frame that a
winning run gets the guest to consume, is the copy this tree hands the guest
self-consistent with the 12-byte RX header the guest's own game reads first -
the header's length field against the byte count reported for the frame, the
TX rate / RSSI words, the payload behind it, the client mask and AID the trace
matched on, and the framing on retransmission (a fresh 802.11 seqno per copy,
no redelivery of one bus stamp) - and how deep into it the guest's own code
read?

Two logs are enough: one where the guest's game consumed the frame (a win) and
one where it did not (a storm). Everything below comes from the `[mp]` lines
already in a `MD_LINK_VERBOSE=1` capture, so a diff can be re-run on any pair
of logs without a live session:

  [mp] cmd seq=.. offered to this console's game: rxflags=.., N byte 802.11
       frame, header[..] at ZZZZ, payload@24[..], clienttime .., mask .., AID ..
  [mp] packet delivered to the console's own software: .. N byte frame, fc=..
  [mp] the console's game reads the command it was handed: cmd seq=..,
       K halfwords read so far, this one at OOOO[=VVVV] of the L byte region
       from ZZZZ

Note on the read record: the trace only *prints* the first six reads of a
frame (plus every 64th), so "six read lines" is not "six reads". The `K
halfwords read so far` counter on the last printed line is the lower bound of
what the game actually took, and `--depth` reports it: that is what separates a
guest that stopped at the frame header from one that read the payload.

Usage:
    tools/scripts/delivery-field-diff.py WIN.log STORM.log [--player 2]   # the guest's `pN` label
    tools/scripts/delivery-field-diff.py A.log.gz B.log.gz --json out.json
"""

import argparse
import collections
import gzip
import json
import re
import sys

OFFER = re.compile(
    r"\[melonDS p(\d):\w+\] \[mp\] cmd seq=(\w+) offered to this console's game: "
    r"rxflags=(\w+), (\d+) byte 802\.11 frame, header\[([0-9A-F ]+)\] at (\w+)"
    r"(?:, payload@24\[([0-9A-F ]*)\])?"
    r"(?:, clienttime (\d+), mask (\w+), AID (\d+))?")

DELIV = re.compile(
    r"\[melonDS p(\d):\w+\] \[mp\] packet delivered to the console's own software: "
    r"(\d+) so far, (\d+) byte frame, fc=(\w+)")

READ = re.compile(
    r"\[melonDS p(\d):\w+\] \[mp\] the console's game reads the command it was handed: "
    r"cmd seq=(\w+), (\d+) halfwords read so far, this one at (\w+)"
    r"(?:=(\w+))? of the (\d+) byte region from (\w+)")

REDELIV = re.compile(r"\[melonDS p(\d):\w+\] \[mp\] REDELIVERED: bus stamp (\d+), "
                     r"(\d+) byte frame, fc=(\w+), seq=(\w+)")


def open_maybe_gz(path):
    return gzip.open(path, "rt", errors="replace") if path.endswith(".gz") else open(path, "rt", errors="replace")


def scan(path, player):
    """Everything one log says about one console's deliveries and reads."""
    offers = []            # dicts, in log order
    delivered = collections.Counter()   # (len, fc) -> count
    reads = collections.defaultdict(lambda: {"lines": 0, "total": 0, "deepest": -1,
                                             "first": [], "len": 0})
    redeliv = 0
    with open_maybe_gz(path) as f:
        for line in f:
            m = OFFER.search(line)
            if m:
                if int(m.group(1)) != player:
                    continue
                hdr = m.group(5).split()
                offers.append({
                    "seq": m.group(2),
                    "rxflags": m.group(3),
                    "len": int(m.group(4)),
                    "header": hdr,
                    "zone": int(m.group(6), 16),
                    "payload": (m.group(7) or "").split(),
                    "mask": m.group(9),
                    "aid": m.group(10),
                })
                continue
            m = DELIV.search(line)
            if m:
                if int(m.group(1)) == player:
                    delivered[(int(m.group(3)), m.group(4))] += 1
                continue
            m = READ.search(line)
            if m:
                if int(m.group(1)) != player:
                    continue
                e = reads[m.group(2)]
                k = int(m.group(3))
                start = int(m.group(7), 16)
                rel = (int(m.group(4), 16) - start) & 0x1FFE
                e["lines"] += 1
                e["total"] = max(e["total"], k)
                e["deepest"] = max(e["deepest"], rel)
                e["len"] = int(m.group(6))
                if len(e["first"]) < 6:
                    e["first"].append((k, rel))
                continue
            m = REDELIV.search(line)
            if m and int(m.group(1)) == player:
                redeliv += 1
    return {"offers": offers, "delivered": delivered, "reads": reads, "redeliveries": redeliv}


def frame_stats(s, body_len):
    """The one frame class under study: the host's body_len-byte payload."""
    want = body_len + 24              # 802.11 header + payload
    fr = [o for o in s["offers"] if o["len"] == want]
    if not fr:
        return None
    seqt = [o["seq"] for o in fr]
    payloads = collections.defaultdict(list)
    hdrs = collections.defaultdict(list)
    for o in fr:
        payloads[tuple(o["payload"])].append(o["zone"])
        hdrs[tuple(o["header"])].append(o["zone"])
    zones = [o["zone"] for o in fr]
    strides = [zones[i + 1] - zones[i] for i in range(len(zones) - 1)]
    common_stride = collections.Counter(strides).most_common(1)
    return {
        "frame_len": want,
        "copies": len(fr),
        "distinct_seq": len(set(seqt)),
        "distinct_payload_prefix": len(payloads),
        "distinct_header": sorted(hdrs),
        "payload_variants": sorted((len(z), tuple(k), min(z), max(z))
                                   for k, z in payloads.items()),
        "header_variants": sorted((len(z), tuple(k), min(z), max(z))
                                  for k, z in hdrs.items()),
        "wrap_edge": 0x2000 - (want + 12),
        "len_field_matches_reported": sum(1 for o in fr
                                          if len(o["header"]) == 6
                                          and int(o["header"][4], 16) == o["len"]),
        "masks": sorted({o["mask"] for o in fr}),
        "aids": sorted({o["aid"] for o in fr}),
        "zone_stride": common_stride[0] if common_stride else None,
        "zone_stride_note": "%d frames, %d distinct gaps, most common %s" % (
            len(fr), len(set(strides)), ["0x%X" % g for g, _ in common_stride[:1]]),
        "delivered_count": s["delivered"].get((want, "0228"), 0),
        "reads": {},
        "redeliveries": s["redeliveries"],
    }


def read_depth(s, body_len):
    want = body_len + 24 + 12         # region = 12-byte RX header + frame
    rows = {seq: e for seq, e in s["reads"].items() if e["len"] == want}
    if not rows:
        return None
    totals = [e["total"] for e in rows.values()]
    deepest = [e["deepest"] for e in rows.values()]
    first = collections.Counter(tuple(e["first"]) for e in rows.values())
    return {
        "frames": len(rows),
        "reads_min": min(totals), "reads_median": sorted(totals)[len(totals) // 2],
        "reads_max": max(totals),
        "deepest_min": min(deepest), "deepest_median": sorted(deepest)[len(deepest) // 2],
        "deepest_max": max(deepest),
        "first_six_most_common": [[[k, o] for k, o in tup] for tup, _ in first.most_common(2)],
        "first_six_share": first.most_common(1)[0][1] / len(rows),
    }


def report(paths, player, body_len):
    out = {}
    for path in paths:
        s = scan(path, player)
        st = frame_stats(s, body_len)
        rd = read_depth(s, body_len)
        if st is not None:
            st["reads"] = rd
        out[path] = st
        print("== %s (console p%d)" % (path, player))
        if st is None:
            print("   no %d-byte frame delivered to this console in this log" % (body_len + 24))
            continue
        print("   copies handed to the game: %d, distinct 802.11 seqno: %d (a repeat would be a"
              " retransmission reusing its frame)" % (st["copies"], st["distinct_seq"]))
        print("   payload prefixes seen: %d, header word sets seen: %d %s"
              % (st["distinct_payload_prefix"], len(st["distinct_header"]),
                 " ".join("[" + " ".join(h) + "]" for h in st["distinct_header"])))
        print("   header length field == reported byte count: %d/%d"
              % (st["len_field_matches_reported"], st["copies"]))
        for label, variants in (("payload prefix", st["payload_variants"]),
                                ("header words", st["header_variants"])):
            if len(variants) > 1:
                print("   %s variants, by count (the non-dominant ones are the diagnostic):"
                      % label)
                for cnt, words, zmin, zmax in sorted(variants, reverse=True)[:12]:
                    edge = " zone >= %04X: region crosses the end of the RAM window" % st["wrap_edge"] \
                        if zmin >= st["wrap_edge"] else ""
                    print("      %5d  zone %04X..%04X  %s%s" % (cnt, zmin, zmax, " ".join(words), edge))
                others = [v for v in variants if v[0] < max(variants)[0]]
                if others and all(v[2] >= st["wrap_edge"] for v in others):
                    print("      -> every non-dominant copy sat in the last region-sized slice of the"
                          " RAM window, so the print ran past its end mid-region and shows the"
                          " bytes beyond the ring, not the frame. Consistent with a trace artifact,"
                          " not a differently built frame (a capture from a build that walks the"
                          " region through IncrementRXAddr settles it: the line then carries"
                          " `WRAPS at the ring end`).")
        print("   client mask %s, AID %s (the trace's own match test), bus redeliveries %d"
              % (",".join(st["masks"]), ",".join(st["aids"]), st["redeliveries"]))
        print("   ring placement: %s" % st["zone_stride_note"])
        if st["zone_stride"] is not None:
            print("      (the gap between two copies is the ring space the frames delivered in"
                  " between them took, not this tree's choice)")
        if rd is None:
            print("   the guest's read recorder saw no read of this frame")
        else:
            print("   guest read record: %d frames, %d-%d halfword reads (median %d),"
                  " deepest offset %d..%d (median %d) of a %d-byte region"
                  % (rd["frames"], rd["reads_min"], rd["reads_max"], rd["reads_median"],
                     rd["deepest_min"], rd["deepest_max"], rd["deepest_median"], body_len + 36))
            for pat in rd["first_six_most_common"]:
                print("      first six accesses (count-so-far, offset): %s" % (pat,))
            print("      frames whose first six accesses were identical: %.0f%%"
                  % (100.0 * rd["first_six_share"]))
            print("      body starts at offset 12; frame control at 12, seq control at 34")
        print()

    vals = [v for v in out.values() if v]
    if len(vals) == 2:
        a, b = vals
        def one_real_variant(st):
            # A variant that only ever appears in the last region-sized slice of RAM
            # is the trace wrapping at 0x2000, not a second frame.
            v = st["payload_variants"]
            dominant = max(v)[0]
            return all(cnt == dominant or zmin >= st["wrap_edge"] for cnt, _, zmin, _ in v)

        same = (one_real_variant(a) and one_real_variant(b)
                and max(a["payload_variants"])[1] == max(b["payload_variants"])[1]
                and a["len_field_matches_reported"] >= a["copies"] - len(a["header_variants"]) + 1)
        print("VERDICT: the copy is %s between the two logs (payload prefix, header words,"
              " length field vs reported count)." % ("identical" if same else "NOT identical"))
        if a["reads"] and b["reads"]:
            ca, cb = a["reads"]["first_six_most_common"][0], b["reads"]["first_six_most_common"][0]
            if ca == cb:
                print("         both guests' code made the same first six accesses %s, then one"
                      " stopped and the other went on: the gate is downstream of those reads."
                      % ca)
        if same:
            print("         So nothing in the delivery explains a decline: the remaining suspect"
                  " is the guest's own state, and the next measurement is what its code does"
                  " after that sixth access.")
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="+", help="melonDS logs (plain or .gz), a win first and a storm second")
    ap.add_argument("--player", type=int, default=2,
                    help="the console's label in the log: 2 studies `p2`, the guest in a 2-console run"
                         " (default 2); a 3- and 4-console log wants the guest's own label")
    ap.add_argument("--body-len", type=int, default=264,
                    help="payload length of the frame under study (default 264: Tetris DS's start packet)")
    ap.add_argument("--json", help="also write the collected numbers here")
    args = ap.parse_args(argv)

    out = report(args.logs, args.player, args.body_len)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(out, f, indent=2, sort_keys=True)
            f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
