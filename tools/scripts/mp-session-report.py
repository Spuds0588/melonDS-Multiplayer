#!/usr/bin/env python3
"""One multiplayer session, read back as numbers instead of prose.

Answers the questions a link pass asks between runs:

  * per-console frame rate over the health windows, and the group's emulated
    seconds against wall-clock seconds (what the reply rendezvous, or any other
    change, costs the group - measured rather than inferred),
  * the link mask over time, with the frames at which it changed,
  * every stall kick / resync the host issued,
  * every deauth, with the console, the emulated time and the reason,
  * the acks the host sent and how many reported a failed client, plus the
    reply-rendezvous counters (rounds held open, give-ups, emulated us held).

    tools/scripts/mp-session-report.py logs/rd3c.log [...]

Reads only the app log (the format tools/run_app.sh writes); missing sections
print as "0" rather than being guessed at.
"""

import re
import sys

START = re.compile(r"^\[(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)Z")
HEALTH = re.compile(
    r"health p(\d+): (.*?) · frames (\d+)\(\+(\d+)\) · .*? slots (0x[0-9A-Fa-f]+)"
    r" · .*?wifi on/off (\d+)/(\d+)")
KICK = re.compile(r"link stall kick: after (\d+) stalled window")
HEART = re.compile(r"heartbeat #\d+:.*?kicks (\d+) · rss (\d+)MB · threads (\d+)")
DEAUTH = re.compile(
    r"\[melonDS p(\d+):(\w+)\] \[mp\] deauth (sent|received): .*? at (\d+) us, .*? reason ([0-9A-F]{4})")
ACK = re.compile(r"\[\w+\]?.*?\[mp\] ack sent: cmdcount (\d+), clientfail ([0-9A-F]{4})")
HOSTDIG = re.compile(
    r"mp p(\d+) \[host\].*?cmds tx (\d+)\(.*?acks (\d+)\(.*?clientfail (\d+)\) · rendezvous (\d+)\("
    r"\+(\d+) gave up, (\d+)us held\)")
# One closed hold, printed per hold with MD_LINK_VERBOSE=1. This is the only
# place the rendezvous' *wall* cost appears: the digest counts emulated
# microseconds held, which says how much of the console's own clock a hold
# spent and nothing about what it cost the group.
HOLD = re.compile(
    r"\[mp\] rendezvous hold: round ([0-9A-Fa-f]{4}), host frame (\d+), "
    r"(\d+) us wall, (\d+) polls for (\d+) us emulated \(([\d.]+) frames\)")
# The host's current command body, printed in the periodic digest. The body is
# how a room state is named from the wire: at two carts the match phase is
# `F6 00 02 00 04 9D`, at four it is `F6 00 0E 00 06 9D` - the same family with
# the client count folded into the phase byte - so counting only `04 9D`
# reports zero matches on a four-console run that is visibly playing
# (2026-10-23).
LASTCMD = re.compile(
    r"mp p(\d+) last: cmd seq ([0-9A-Fa-f]+) len \d+ body ((?:[0-9A-F]{2} ){6})")


def ts(ms):
    return ms


for path in sys.argv[1:]:
    lines = open(path, "rb").read().decode("utf-8", "replace").splitlines()

    windows = {}       # player -> [(frames_delta, mask, wifi_on)]
    families = {}      # (mask, phase, opcode) -> [count, {seqnos already counted}]
    kicks = 0
    heart = None
    holds = []         # (frame, wall_us, polls, emulated_us)
    host_frames_seen = set()
    deauths = []
    acks = 0
    ack_masks = []
    host = None
    first_us = last_us = None
    for ln in lines:
        m = HEALTH.search(ln)
        if m:
            p = int(m.group(1))
            windows.setdefault(p, []).append(
                (int(m.group(4)), m.group(5), m.group(6) == "1"))
            continue
        m = KICK.search(ln)
        if m:
            kicks += 1
            continue
        m = HEART.search(ln)
        if m:
            heart = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
            continue
        m = DEAUTH.search(ln)
        if m:
            deauths.append((int(m.group(1)), m.group(3), int(m.group(4)), m.group(5)))
            continue
        m = ACK.search(ln)
        if m:
            acks += 1
            ack_masks.append(int(m.group(2), 16))
            continue
        m = LASTCMD.search(ln)
        if m:
            body = bytes.fromhex(m.group(3).replace(" ", ""))
            key = (body[2], body[4], body[5])
            ent = families.setdefault(key, [0, set()])
            seq = m.group(2).upper()
            if seq not in ent[1]:
                ent[1].add(seq)
                ent[0] += 1
            continue
        m = HOLD.search(ln)
        if m:
            holds.append((int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))))
            host_frames_seen.add(int(m.group(2)))
            continue
        m = HOSTDIG.search(ln)
        if m:
            # The digest prints running totals for the deferred count and deltas
            # for the give-ups and held time, so those two are summed here.
            prev = host
            host = (int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)),
                    (prev[4] if prev else 0) + int(m.group(6)),
                    (prev[5] if prev else 0) + int(m.group(7)))

    print(f"== {path}")
    if heart:
        print(f"   heartbeat: kicks {heart[0]}, rss {heart[1]}MB, threads {heart[2]}")
    print(f"   stall kicks: {kicks}")
    if acks:
        failed = sum(1 for v in ack_masks if v)
        print(f"   acks {acks}, with a failed client {failed} ({100.0 * failed / acks:.1f}%)")
    if host:
        print(f"   host digest: cmds tx {host[0]}, acks {host[1]}, clientfail {host[2]}, "
              f"rendezvous {host[3]} ({host[4]} gave up, {host[5]}us held)")
    if holds:
        wall = [h[1] for h in holds]
        polls = [h[2] for h in holds]
        emu = [h[3] for h in holds]
        mean_wall = sum(wall) / len(wall)
        mean_emu = sum(emu) / len(emu) or 1
        print(f"   rendezvous holds: {len(holds)} over {len(host_frames_seen)} host frames "
              f"({len(holds) / max(1, len(host_frames_seen)):.2f} per frame)")
        print(f"     wall: {sum(wall) / 1e6:.2f}s total, mean {mean_wall / 1000:.1f}ms, "
              f"max {max(wall) / 1000:.1f}ms · emulated: {sum(emu) / 1e6:.2f}s total, "
              f"mean {mean_emu / 1000:.2f}ms ({mean_emu / 16715:.2f} frames)")
        print(f"     polls per hold: mean {sum(polls) / len(polls):.0f}, max {max(polls)} · "
              f"wall per emulated frame held: {mean_wall / (mean_emu / 16715) / 1000:.1f}ms")
    if families:
        print("   command bodies the host carried (distinct seqno):")
        for (mask, phase, op), (n, _) in sorted(
                families.items(), key=lambda kv: (kv[0][2] != 0x9D, -kv[1][0]))[:8]:
            print(f"     F6 00 {mask:02X} 00 {phase:02X} {op:02X} x{n}")
        # The play phase, by cart count. The family is `F6 00 <mask> 00 <phase>
        # 9D`, and the phase byte follows the number of clients in the room
        # (04 at two carts, 05 at three, 06 at four): counting only 04 reports
        # zero on a four-console run that is playing.
        play = {4: "2 carts", 5: "3 carts", 6: "4 carts"}
        for phase, label in play.items():
            n = sum(v[0] for (m, p, o), v in families.items() if p == phase and o == 0x9D)
            if n:
                print(f"     play phase {phase:02X} ({label}): {n} digest sample(s)")
    if deauths:
        print(f"   deauths: {len(deauths)}")
        for p, kind, t, reason in deauths[:12]:
            print(f"     p{p} {kind} reason {reason} at {t / 1e6:.2f} emulated s")
        if len(deauths) > 12:
            print(f"     ... {len(deauths) - 12} more")

    fps = {}
    for p, rows in sorted(windows.items()):
        deltas = [r[0] for r in rows if r[0] > 0]
        if not deltas:
            continue
        fps[p] = (min(deltas) / 5.0, sum(deltas) / len(deltas) / 5.0)
    if fps:
        print("   fps per console (min / mean over 5s windows): "
              + ", ".join(f"p{p} {lo:.1f}/{mean:.1f}" for p, (lo, mean) in fps.items()))

    masks = {}
    for p, rows in sorted(windows.items()):
        seq = [(r[1], r[2]) for r in rows]
        changes = [seq[0]] + [seq[i] for i in range(1, len(seq)) if seq[i] != seq[i - 1]]
        masks[p] = changes
    if masks:
        print("   slots/wifi timeline (per console):")
        for p, ch in sorted(masks.items()):
            print(f"     p{p}: " + " -> ".join(
                f"{mask}{'' if on else ' (wifi off)'}" for mask, on in ch))
