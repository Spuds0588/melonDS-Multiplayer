#!/usr/bin/env python3
"""Sample *every* console's own lobby page at one emulated instant, once a second.

The three-console MPH question is what each console's own game believes, and the
only cheap instrument for that is the picture: every console is drawn into one
composite 256x384 column (console N at x (N-1)*256, screens stacked top 0..191 /
bottom 192..383), so **one** `shot` reads all of them as of the same emulated
frame - three separate reads would be three different instants, which is exactly
the confound this is meant to remove. `tools/scripts/lobby_page.py` classifies
those columns; this file only takes the samples and stamps them.

Each sample is bracketed by the app's own frame counters, so a line can be placed
on the emulated clock and not just on the wall clock:

    [12:34:56.7] f 2836/2836/2838..2838/2838/2840 mask 0x7 | p1 page=MODE ... | p2 ...

and, unless `--no-links`, a second line per sample carries each console's *own*
link state from `probe` at that instant (its own frame counter included, so the
column is on the emulated clock too):

    L f 2836/2837/2838 | p1 role=1 tx 1204(+0)/ack 1204(fail 0) win 601/0/0/0/2/0
      wr 1204 ans 0 arms 0 aids 0 rx 1204(dup 0) d 0/0:0000 cmd 04B0/6b an 6b
      | p2 role=2 ... | p3 ...

  role   0 none, 1 host, 2 client
  tx/ack/ac(dup,fail)  commands this console transmitted / acks it sent and how
         many of those reported a client failing to answer
  win    its reply window: collected / empty / blank / stale / reanchor-dropped /
         not-looked; `wr` is what its own game handed the bus and `ans`, `arms`,
         `aids` what a client's game produced, armed and set as its own AID
  rx     command frames delivered to its game, and how many repeated a seqno
  d      deauth frames sent/accepted and the last reason code seen
  cmd/an the last command and answer bodies' lengths (the games' own state)

That pairing is what separates the two candidate causes of a guest's own deauth:
a host that stops a page, stops sending or stops acknowledging (visible on p1's
column first) against a guest whose own game walks away while the host is still
rolling (visible as the guest's column going quiet first, host unchanged).

Usage:
    tools/scripts/lobby-track.py --seconds 90                  # 1/s for 90 s
    tools/scripts/lobby-track.py --seconds 200 --interval 1.5 --out logs/v9k_track.log
    tools/scripts/lobby-track.py --players 2 --port 7373

It talks to the app's control channel (MD_CONTROL_PORT, default 7373) through the
same `Channel` tools/appctl.py uses, and it is safe to run *alongside* a driver
such as tools/scripts/mph-match.sh: one connection each, distinct shot names, and
every call is read-only.
"""

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
for p in (TOOLS, HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

import appctl          # noqa: E402  (the control channel and the page reader)
import lobby_page      # noqa: E402


def frame_counters(ch):
    """The per-console emulated frame counter, as a list of ints (1 per console)."""
    st = ch.call(cmd="status")
    if not st.get("active"):
        raise SystemExit("(no session: no console is running)")
    return [f["seq"] for f in st["frames"]], st.get("link_mask", 0)


def sample(ch, name):
    """One instant: the pages of every console, plus the frames on either side."""
    before, mask_before = frame_counters(ch)
    out = ch.call(cmd="shot", name=name)
    after, mask_after = frame_counters(ch)
    pages = [lobby_page.line(out["path"], col) for col in range(len(before))]
    return before, after, mask_before, mask_after, pages


def link_state(ch, players):
    """Each console's own link counters, read back-to-back as one column each.

    Returns (frames, columns): the frame each probe reported for that console,
    so the line says which emulated instant each column was read at.
    """
    frames = []
    cols = []
    for p in range(players):
        try:
            pr = ch.call(cmd="probe", player=p + 1)   # the control channel is 1-based
        except Exception as exc:              # a console that has stopped running
            frames.append("?")
            cols.append(f"p{p + 1} probe failed: {exc}")
            continue
        frames.append(pr.get("frames"))
        wifi = pr.get("wifi", {})
        mp = pr.get("mp", {})
        cols.append(
            f"p{p + 1} role={wifi.get('role')} f{pr.get('frames')} "
            f"tx {mp.get('cmds_tx')}(+{mp.get('cmds_tx_dup')})/ack {mp.get('acks')}"
            f"(fail {mp.get('clientfail')}) "
            f"win {mp.get('collected')}/{mp.get('empty')}/{mp.get('reply_blank')}/"
            f"{mp.get('stale')}/{mp.get('reanchor_dropped')}/{mp.get('not_looking')} "
            f"wr {mp.get('reply_written')} rx {mp.get('cmds_rx')}(dup {wifi.get('cmds_rx_dup')}) "
            f"ans {wifi.get('answered')}(blank {wifi.get('answers_blank')}) "
            f"arms {wifi.get('arms')} aids {wifi.get('aids_set')} "
            f"d {mp.get('deauth_out')}/{mp.get('deauth_in')}:{mp.get('deauth_reason')} "
            f"cmd {mp.get('cmd_seq')}/{mp.get('cmd_len')}b an {mp.get('answer_len')}b")
    return frames, cols


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=90.0,
                    help="how long to sample for (default 90)")
    ap.add_argument("--interval", type=float, default=1.0,
                    help="seconds between samples (default 1.0; each sample costs "
                         "~0.6 s on its own, so this is a floor, not a sleep)")
    ap.add_argument("--players", type=int, default=3,
                    help="consoles to read (default 3)")
    ap.add_argument("--port", type=int, default=appctl.DEFAULT_PORT,
                    help="control channel port (default %(default)s)")
    ap.add_argument("--out", default=None,
                    help="also append the timeline to this file (the same lines go to stdout)")
    ap.add_argument("--no-links", action="store_true",
                    help="skip the per-console link columns (one probe per console per sample)")
    args = ap.parse_args()

    ch = appctl.Channel(args.port)
    sink = open(args.out, "a", encoding="utf-8") if args.out else None

    def say(text):
        print(text, flush=True)
        if sink:
            sink.write(text + "\n")
            sink.flush()

    started = time.time()
    say(f"--- lobby-track start {time.strftime('%H:%M:%S')} "
        f"({args.seconds:g}s, {args.interval:g}s interval, players {args.players}, "
        f"port {args.port})")
    n = 0
    try:
        while time.time() - started < args.seconds:
            tick = time.time()
            before, after, mask_b, mask_a, pages = sample(ch, "track")
            n += 1
            frames = "/".join(str(v) for v in before[:args.players])
            span = "" if before == after else ".." + "/".join(str(v) for v in after[:args.players])
            # The mask on either side of the shot says whether a console left
            # *during* the read, which is the one way a page can be post-mortem.
            mask = f"0x{mask_b:x}" if mask_b == mask_a else f"0x{mask_b:x}->0x{mask_a:x}"
            say(f"[{time.strftime('%H:%M:%S')}.{int(tick % 1 * 1000):03d}] "
                f"f {frames}{span} mask {mask} | "
                + " | ".join(f"p{i + 1} {line}" for i, line in enumerate(pages[:args.players])))
            if not args.no_links:
                link_frames, cols = link_state(ch, args.players)
                say("    L f " + "/".join(str(v) for v in link_frames)
                    + " | " + " | ".join(cols))
            wait = args.interval - (time.time() - tick)
            if wait > 0:
                time.sleep(wait)
    except KeyboardInterrupt:
        pass
    finally:
        say(f"--- lobby-track stop {time.strftime('%H:%M:%S')} after {n} sample(s)")
        ch.close()
        if sink:
            sink.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
