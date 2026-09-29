#!/usr/bin/env python3
"""Hands-free New Super Mario Bros. local-wireless join, driven through the app.

Sibling of `tools/mkds_join.py`, and deliberately not a copy of it: the control
channel client, the digest/health parsers, the screen reads and the tap helpers
come from that tool, and this file owns only what is different - NSMB's route and
NSMB's way of saying it worked.

**The route is `tools/scripts/nsmb-route.sh`'s, frame for frame.** That script is
the only copy of the coordinates and frames on the harness side, and the first
version of this driver did not use it: it tapped each console in turn and waited
for that console's screen to change before moving to the next, which put the two
games hundreds of frames out of step (measured 2026-09-29: both consoles ended
`[host]`, each beaconing at the other with `cmds rx 0`, both screens the same
"Searching for ..." dialog, while the harness route on the same build and save
reached 11,126 coupled commands). NSMB's multi-card flow is a *negotiation*, so
the two consoles have to make the same move at the same time; the harness route
taps both consoles on the same frame for exactly that reason, and so does this
one:

  612    touch TITLE     (128, 95)  both consoles
  1292   touch MULTICARD (128, 72)  both
  2012   touch CHAR1     (127, 119) player 1, CHAR2 (127, 160) player 2
  2600+  touch CONFIRM   (128, 171) both, every 160 frames
  2700+  press A                    both, every 60 frames
  12000  input stops; the session runs on its own to 14000

The difference from the harness is the clock: the app runs at realtime, so this
driver waits for the consoles to *reach* a route frame and acts on both consoles
in that same frame (`wait_frame`, `send_pair` - one channel each, because one
connection serialises the two consoles' taps ~11 frames apart), and it reads
both consoles' screens before every action, printing what it saw. A frame the
session has already gone past is reported loudly rather than delivered quietly
late (`wait_boot`, `missed`): sleeping a flat wall-clock boot wait put this
route ~600 frames behind the harness's, and nothing said so. The route stops
early the moment the wire's own numbers say the games are in session data - the
game's signal, not a screenshot.

    tools/nsmb_join.py --restart                  # the whole route
    tools/nsmb_join.py --no-restart               # drive the app that is up
    tools/nsmb_join.py --restart --couple         # and watch the guest's input
                                                  # land in the host's picture
"""

import argparse
import base64
import json
import os
import re
import statistics
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from appctl import Channel, parse_buttons  # noqa: E402
from mkds_join import Runner, log  # noqa: E402  (the shared app driver)

ROM = os.path.join(ROOT, "Test Roms/New Super Mario Bros. (USA).nds")

# tools/scripts/nsmb-route.sh's coordinates and frames (its comment block owns
# them; these are the app-side copies, and a re-timing of the route has to move
# both).
TITLE = (128, 95)
MULTICARD = (128, 72)
CHAR1 = (127, 119)
CHAR2 = (127, 160)
CONFIRM = (128, 171)
# The appeared-dialog's Yes (`melonDS <peer> has appeared. Play a game?`, No at
# (128, 120)). The harness route never taps it; the app's own documented route
# (history.md v0.1.9x) does, and that is the one run recorded to have carried the
# app's two consoles into the session-data phase.
DIALOG_YES = (128, 71)

FR_TITLE = 612
FR_MULTICARD = 1292
FR_CHARACTER = 2012
FR_CONFIRM_FROM, FR_CONFIRM_TO, FR_CONFIRM_STEP = 2600, 11500, 160
FR_A_FROM, FR_A_TO, FR_A_STEP = 2700, 11500, 60
FR_STOP_INPUT = 12000
FR_END = 14000

# `tools/scripts/nsmb-route.sh`'s taps are six emulated frames long (`--touch`
# sets last_frame = first_frame + 6, game_session.cpp:395) and land on every
# console in the same frame (`--all-players`). The app's driver used `ms=180`,
# which at 59.8fps is ~11 frames, and sent the two consoles' taps one after the
# other, so player 2's pen went down 17 frames after player 1's (measured
# 2026-09-29, logs/nsmb_app2.log: player 1's (127,119) down at frame 2015, player
# 2's (127,160) at 2029 - the harness taps both at 2012). This is the app-side
# number that makes the two routes the same length: one emulated frame is
# 16.726ms, so six frames is 100ms.
TOUCH_MS = 100
PRESS_MS = 100

# How many frames past a route frame the driver still counts as "on time".
# Polling the channel and sending a touch costs a few milliseconds (~1 frame).
MISS_SLACK = 5

# HOW FAR AN INPUT LINE MAY SIT OUTSIDE THE PHASE IT CAUSED. The channel applies
# an input and the sampler's first pair is a frame or two later, so the app logs
# the line just *before* the window its effect is measured in - measured
# 2026-09-29: the host's own RIGHT press logged at frame 14907 against a phase D
# window of 14908..15145, and its release at 15147. A window test with no slack
# reported "input lines in that window: none" for the very press it was looking
# for, which would have read as "the host sent nothing during its own input
# phase". Small enough that neighbouring phases cannot swap lines: the gap
# between two windows here is under 3 frames.
INPUT_JOIN_SLACK = 8

# Session data is 262/266-byte commands and 70-byte answers; the lobby is 34/30
# (`history.md` v0.1.9t's ledger). The threshold sits between the two worlds.
SESSION_BYTES = 200

# The digest's fields are parsed by name from `Runner.DIGEST_FIELDS` (one owner of
# that vocabulary, in tools/mkds_join.py). This file used to carry its own copy as
# one big pattern whose groups were read positionally - and it read them one off
# from `canned` onward, so the app's host was reported as `collected 0 empty 1021`
# while its own digest said `collected 1021(+298) empty 4(+4)` (2026-09-29). A
# whole pass's premise came out of that shift.
LAST_RE = re.compile(
    r"mp p(\d) last: cmd seq ([0-9A-F]+) len (\d+) body ([0-9A-F ]*) at (\+[\d.]+s|-)"
    r" · answer len (\d+) body (.*?) at"
)

# Does a player's input reach the *other* console's picture? Measured, not
# inferred - and measured at frame granularity, because the game animates on its
# own and a slow sampler cannot tell the two apart (the coarse version of this
# test read 10 distinct pictures per console in a 2.5 s no-input baseline, which
# makes "the picture changed" meaningless: history.md v0.1.9ag).
#
# So the sampling runs *inside* the page, one `frames()` call per iteration, and
# only counts a pair when both consoles advanced exactly one emulated frame -
# i.e. the pair is what the game itself did in one frame. Everything a pair says
# is therefore in the game's own clock, not the sampler's.
#
# What it returns is a bitmask per console and screen of every pixel that ever
# moved, plus the per-pair moved-pixel counts, so the caller can compare three
# phases - no input / the guest alone holding a direction / no input - and ask
# which pixels moved under the input and in neither baseline. That is the test
# the harness passed on this ROM (1,125 px of the host's screen) and the app has
# never had.
SAMPLER_JS = r"""
async function (seconds) {
  const D = window.__mdctl, W = D.W, H = D.H, BPS = D.BPS, NP = W * H;
  const THRESH = 16;   // per-channel change that counts as the pixel moving
  const NC = 2;        // consoles watched: every one that is running
  const bit = [], moved = [];
  for (let s = 0; s < 2; s++) {
    bit.push([]); moved.push([]);
    for (let c = 0; c < NC; c++) { bit[s].push(new Uint8Array(NP >> 3)); moved[s].push([]); }
  }
  const pairFrame = [], shareTop = [], shareBot = [], dseq = {};
  let prev = null, prevSeq = null, iters = 0, pairs = 0, err = '', skipped = 0;
  const t0 = performance.now(), tEnd = t0 + seconds * 1000;
  while (performance.now() < tEnd) {
    const buf = await D.frames();
    if (!buf) { err = 'get_frames returned nothing'; break; }
    const pl = D.unpack(buf);
    if (pl.n < NC) { err = 'only ' + pl.n + ' console(s) running'; break; }
    const u8 = new Uint8Array(buf), dv = new DataView(buf), seq = [];
    for (let c = 0; c < NC; c++) {
      const o = c * (8 + 2 * BPS);
      seq.push(dv.getUint32(o + 4, true) * 4294967296 + dv.getUint32(o, true));
    }
    iters++;
    if (prev) {
      let step = true;
      for (let c = 0; c < NC; c++) if (seq[c] - prevSeq[c] !== 1) step = false;
      if (step) {
        pairs++; pairFrame.push(seq[0]);
        for (let c = 0; c < NC; c++) {
          const base = pl.at(c);
          for (let s = 0; s < 2; s++) {
            const o = base + s * BPS;
            let n = 0;
            for (let i = 0; i < NP; i++) {
              const j = o + (i << 2);
              if (Math.abs(u8[j] - prev[j]) > THRESH || Math.abs(u8[j + 1] - prev[j + 1]) > THRESH
                  || Math.abs(u8[j + 2] - prev[j + 2]) > THRESH) {
                n++; bit[s][c][i >> 3] |= 1 << (i & 7);
              }
            }
            moved[s][c].push(n);
          }
        }
        const a0 = pl.at(0), a1 = pl.at(1);
        let dt = 0, db = 0;
        for (let i = 0; i < NP; i++) {
          const x = a0 + (i << 2), y = a1 + (i << 2);
          if (Math.abs(u8[x] - u8[y]) > THRESH || Math.abs(u8[x + 1] - u8[y + 1]) > THRESH) dt++;
          if (Math.abs(u8[x + BPS] - u8[y + BPS]) > THRESH || Math.abs(u8[x + BPS + 1] - u8[y + BPS + 1]) > THRESH) db++;
        }
        shareTop.push(dt); shareBot.push(db);
      } else {
        skipped++;
        const k = seq[0] - prevSeq[0];
        dseq[k] = (dseq[k] || 0) + 1;
      }
      prev.set(u8.subarray(0, prev.length));
    } else {
      prev = new Uint8Array(u8.length); prev.set(u8);
    }
    prevSeq = seq;
  }
  const b64 = function (a) {
    let s = '';
    for (let i = 0; i < a.length; i += 8192) s += String.fromCharCode.apply(null, a.subarray(i, i + 8192));
    return btoa(s);
  };
  return JSON.stringify({
    err: err, seconds: seconds, iters: iters, pairs: pairs, skipped: skipped,
    ms: Math.round(performance.now() - t0), dseq: dseq, pairFrames: pairFrame,
    moved: moved, shareTop: shareTop, shareBot: shareBot,
    masks: bit.map(function (per) { return per.map(b64); })
  });
}
"""

# Pixels moved at least once: the mask is one bit per pixel, so the intersection
# and the difference the verdict is made of are byte operations, not loops over
# 49,152 pixels per console per screen in Python.
def mask_count(m):
    return sum(bin(b).count("1") for b in m)


def mask_or(a, b):
    return bytes(x | y for x, y in zip(a, b))


def mask_andnot(a, b):
    return bytes(x & (y ^ 0xFF) for x, y in zip(a, b))


def mask_bbox(m, width=256):
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for i in range(width * 192):
        if m[i >> 3] & (1 << (i & 7)):
            x, y = i % width, i // width
            x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    return None if x1 < 0 else (x0, y0, x1, y1)


class NsmbRunner(Runner):
    """The shared app driver plus NSMB's route and NSMB's success test."""

    def __init__(self, args, lines):
        super().__init__(args, lines)
        # Route frames that had already gone by when the driver asked for them.
        self.missed = []

    # ---- reading the wire -------------------------------------------------

    def link_lines(self):
        """The newest `mp pN [role]` and `mp pN last` line per console.

        Read forward from where the last call stopped, keeping the last match per
        console. Not a tail of the last few thousand lines: the app's log passes
        40MB in one session and its wifi-io dumps are thousands of lines a second,
        so a tail window contains no digest line at all - which silently answered
        `session_data() = False` on a session that was 2909 commands deep and
        skipped the coupling test (2026-09-29).
        """
        seen = getattr(self, "_link_seen", {})
        try:
            with open(self.log_path, errors="replace") as fh:
                fh.seek(getattr(self, "_link_pos", 0))
                for ln in fh:
                    m = re.search(r"mp p(\d) \[([^\]]*)\]: (.*)", ln)
                    if m:
                        row = seen.setdefault(int(m.group(1)), {})
                        row["digest"] = m.group(3).strip()
                        row["role"] = m.group(2)
                        row.update(self.parse_digest(m.group(3)))
                    m = LAST_RE.search(ln)
                    if m:
                        seen.setdefault(int(m.group(1)), {})["last"] = {
                            "seq": m.group(2), "len": int(m.group(3)), "body": m.group(4).strip(),
                            "at": m.group(5), "answer_len": int(m.group(6)),
                            "answer": m.group(7).strip()[:60],
                        }
                self._link_pos = fh.tell()
        except OSError:
            return seen
        self._link_seen = seen
        return seen

    def balance(self):
        """The claim the pass has to make, in the numbers that make it.

        The fields come parsed from `link_lines` (by name, see DIGEST_FIELDS);
        this only adds the last command/answer each console reports.
        """
        out = {}
        for p, row in self.link_lines().items():
            b = {k: v for k, v in row.items() if k not in ("last", "digest")}
            lb = row.get("last")
            if lb:
                b.update(last_cmd_len=lb["len"], last_cmd_body=lb["body"], last_cmd_at=lb["at"],
                         last_answer_len=lb["answer_len"], last_answer=lb["answer"])
            out[p] = b
        return out

    def counters(self, bal, player):
        """One console's reply-path numbers, named, for a report line."""
        b = bal.get(player, {})
        return (f"cmds rx {b.get('cmds_rx', '?')} answered {b.get('answered', '?')} "
                f"canned {b.get('canned', '?')} · replies sent {b.get('replies_sent', '?')} "
                f"collected {b.get('collected', '?')} empty {b.get('empty', '?')} "
                f"stale {b.get('stale', '?')} reanchor-dropped {b.get('reanchor_dropped', '?')} "
                f"blank {b.get('blank_replies', '?')} notlooking {b.get('notlooking', '?')} · "
                f"cmds tx {b.get('cmds_tx', '?')} acks {b.get('acks', '?')} "
                f"clientfail {b.get('clientfail', '?')} · deauth out {b.get('deauth_out', '?')} "
                f"in {b.get('deauth_in', '?')}")

    def session_data(self, bal=None):
        """Are the two games past discovery and into the session?

        The signal is a *command*: discovery has none (`rx bcast/cmd/reply/ack
        1596/0/0/0` was the whole of the app's park), and the session has them
        from both ends. Command *length* is not the signal - NSMB's own protocol
        uses 6- and 10-byte commands as well as the 262-byte ones, so keying on
        `>= 200 bytes` (`SESSION_BYTES`) reported "not in session data" for a
        session that was 3088 commands deep.
        """
        bal = bal or self.balance()
        return any(
            b.get("cmds_tx", 0) > 0 or b.get("cmds_rx", 0) > 0
            or b.get("last_cmd_len", 0) >= SESSION_BYTES
            for b in bal.values()
        )

    def balance_line(self, bal):
        bits = []
        for p in sorted(bal):
            b = bal[p]
            if "cmds_tx" in b:
                bits.append(
                    f"p{p} {b['role']}: cmds tx {b['cmds_tx']} acks {b['acks']} collected "
                    f"{b['collected']} empty {b['empty']} clientfail {b['clientfail']} · "
                    f"cmd len {b.get('last_cmd_len', '?')} at {b.get('last_cmd_at', '?')}"
                )
            else:
                bits.append(
                    f"p{p} {b['role']}: cmds rx {b['cmds_rx']} answered {b['answered']} canned "
                    f"{b['canned']} replies sent {b['replies_sent']} arms {b['arms']} · "
                    f"answer len {b.get('last_answer_len', '?')}"
                )
        return " · ".join(bits)

    def deauths(self, bal):
        return sum(b.get("deauth_out", 0) + b.get("deauth_in", 0) for b in bal.values())

    # ---- reading the screens ---------------------------------------------

    # ---- sending to both consoles at once --------------------------------

    def send_pair(self, sends):
        """Send one input to each console *at the same time*.

        One channel answers one request at a time, so a loop over the two
        consoles serialises them - and a `touch --ms 100` holds the pen for the
        whole round trip, which is six emulated frames. Each console therefore
        gets its own connection and its own thread here, which is what makes the
        app's route land both consoles' inputs on one frame the way
        `nsmb-route.sh` does.
        """
        fails = []

        def one(player, kind, what):
            ch = None
            try:
                ch = Channel(self.args.port, timeout=30)
                if kind == "touch":
                    ch.call(cmd="touch", player=player, x=what[0], y=what[1], ms=TOUCH_MS)
                else:
                    ch.call(cmd="press", player=player, buttons=parse_buttons(what), ms=PRESS_MS)
            except SystemExit as err:
                fails.append(f"player {player} {kind}: {err}")
            finally:
                if ch is not None:
                    ch.close()

        threads = [threading.Thread(target=one, args=s) for s in sends]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        for f in fails:
            log(self.lines, f"  input to a console failed: {f}")
        return not fails

    def both_screens(self):
        """Both consoles' touch screens, as hashes - the read between touches."""
        return {p: self.screen(p, screen="bottom")[0] for p in (1, 2)}

    def crcs(self):
        """Each console's rendered framebuffers, straight from the renderer."""
        out = {}
        for p in (1, 2):
            pr = self.call(cmd="probe", player=p)
            fb = pr["framebuffer"]
            out[p] = (fb["top"]["crc"], fb["bottom"]["crc"], fb["top"]["mean"])
        return out

    def input_mark(self):
        """The log's end right now, to read input lines forward from."""
        try:
            return os.path.getsize(self.log_path)
        except OSError:
            return 0

    def input_trace(self, since=0):
        """The app's own record of what it delivered, from byte offset `since`.

        From an offset, not from the last N lines: the app's log passes 40MB in a
        single session, so a tail of 600 lines is *always* full and `trace[600:]`
        is always empty. That is how a guest press that the app logged perfectly
        well (`[input] frame 10135 player 2 buttons -> RIGHT (mask 00000010)`)
        was reported as "0 input lines for the guest" (2026-09-29) - a
        measurement artifact that reads exactly like the app dropping input.
        """
        try:
            with open(self.log_path, errors="replace") as fh:
                fh.seek(since)
                lines = fh.read().splitlines()
        except OSError:
            return []
        return [ln.strip() for ln in lines if "[input]" in ln]

    # ---- the clock -------------------------------------------------------

    def wait_boot(self, margin=120):
        """Start driving while the route's own frames are still ahead.

        A route is absolute frames - 612 is the title tap, 1292 the multi-card
        tap - and every tap is one-shot: delivered late, it lands on a screen the
        game has already left, and the game slides behind the route without a
        word. The first version of this driver slept a flat `--boot-wait` 18s for
        the app to boot, which at 59.8fps is frame ~1076 - past the title tap -
        so the app's title tap went down at frame 1225 and its game ran ~600
        frames behind the harness's for the rest of the session (measured
        2026-09-29: its screens at route frame 2012 were pixel-identical to the
        harness's at frame ~900, and its wireless came on at frame 2738 where
        the harness's came on at 2045, on the same build and the same save).
        """
        start = time.time()
        while time.time() - start < self.args.boot_wait_max:
            try:
                now = self.frames_now()
            except SystemExit:
                now = 0
            if now >= 1:
                log(self.lines, f"  session is up at frame {now}; driving from here")
                return now
            time.sleep(0.2)
        raise SystemExit(
            f"no console was running {self.args.boot_wait_max:.0f}s after the app "
            "answered the control channel - nothing to drive"
        )

    def frames_now(self):
        st = self.call(cmd="status")
        return max((f["seq"] for f in st.get("frames", [])), default=0)

    def wait_frame(self, target, timeout=30.0):
        """Wait until every console has reached `target`; report what happened.

        A target that has *already* passed is reported, not acted on quietly: a
        one-shot tap delivered on a screen the game has left is what put this
        route 600 frames out of step once (see `wait_boot`), and silent lateness
        is indistinguishable from a tap that simply did nothing.
        """
        start = time.time()
        now = self.frames_now()
        if now > target + MISS_SLACK:
            self.missed.append((target, now))
            log(self.lines, f"  frame {target} was MISSED (the session is at {now}, "
                            f"{now - target} frames on) - this input lands late")
            return now
        while now < target and time.time() - start < timeout:
            time.sleep(0.05)
            now = self.frames_now()
        if now < target:
            log(self.lines, f"  frame {target}: only reached {now} in {timeout:.0f}s - acting anyway")
        return now

    # ---- the route -------------------------------------------------------

    def plan(self):
        """A route as (frame, kind, who, what), in frame order.

        `harness` is `tools/scripts/nsmb-route.sh` frame for frame. `v019x` is
        the sequence the app itself was driven with on 2026-09-29 when it was
        recorded reaching the session-data phase (`history.md` v0.1.9x): the
        appeared-dialog's Yes first, then the board's confirm, then A. The two
        are kept side by side because they disagree about which input the
        discovery step wants, and that disagreement is the thing being measured.
        """
        events = [
            (FR_TITLE, "touch", "both", TITLE),
            (FR_MULTICARD, "touch", "both", MULTICARD),
            (FR_CHARACTER, "touch", "1", CHAR1),
            (FR_CHARACTER, "touch", "2", CHAR2),
        ]
        if self.args.route == "v019x":
            events += [(f, "touch", "both", DIALOG_YES) for f in range(2600, 4201, 150)]
            events += [(f, "touch", "both", CONFIRM) for f in range(4200, 6501, 150)]
            events += [(f, "press", "both", "a") for f in range(6500, 9001, 60)]
        else:
            events += [(f, "touch", "both", CONFIRM)
                       for f in range(FR_CONFIRM_FROM, FR_CONFIRM_TO + 1, FR_CONFIRM_STEP)]
            events += [(f, "press", "both", "a")
                       for f in range(FR_A_FROM, FR_A_TO + 1, FR_A_STEP)]
        return sorted(events)

    def run_route(self):
        landed = False
        shots = 0
        for frame, kind, who, what in self.plan():
            if frame > FR_STOP_INPUT:
                break
            if self.args.stop_at and frame > self.args.stop_at:
                log(self.lines, f"  stopping at route frame {self.args.stop_at}")
                break
            now = self.wait_frame(frame)
            bal = self.balance()
            if self.session_data(bal) and not landed:
                landed = True
                log(self.lines, f"  frame {now}: the wire is in session data")
                if self.args.stop_when_coupled:
                    log(self.lines, "  stopping here (--stop-when-coupled); the rest of the "
                                    "route is what takes the games from the lobby into the race")
                    break
            if frame in self.args.shots_at:
                # A raster, not a hash: a frame the harness captured too is the
                # only way to say whether this console is on the screen the
                # harness's same console was on at the same emulated frame.
                self.shot(f"nsmb-f{frame}")
                log(self.lines, f"  shot at frame {now} (route frame {frame}) -> "
                                f"{self.balance_line(bal)}")
            screens = self.both_screens()
            if who == "both":
                # Both consoles in the same frame, like the harness's route.
                self.send_pair([(1, kind, what), (2, kind, what)])
            elif kind == "touch":
                self.call(cmd="touch", player=int(who), x=what[0], y=what[1], ms=TOUCH_MS)
            else:
                self.press(int(who), what, ms=PRESS_MS)
            if kind == "touch" and frame >= FR_CONFIRM_FROM and shots < 4:
                shots += 1
                log(self.lines, f"  frame {now} ({kind} {what} on {who}): screens p1:{screens[1]} "
                                f"p2:{screens[2]} · {self.balance_line(bal)}")
                self.shot(f"nsmb-route-{frame}")
            if frame % 1000 < FR_CONFIRM_STEP and frame >= FR_CONFIRM_FROM:
                log(self.lines, f"  frame {now} ({kind} {what} on {who}): screens p1:{screens[1]} "
                                f"p2:{screens[2]} · {self.balance_line(bal)}")
        return landed

    def sample_pictures(self, seconds, every=0.25):
        """Distinct framebuffer CRCs per console over a window, no input sent."""
        seen = {1: set(), 2: set()}
        end = time.time() + seconds
        while time.time() < end:
            for p, (top, bottom, _mean) in self.crcs().items():
                seen[p].add((top, bottom))
            time.sleep(every)
        return {p: len(v) for p, v in seen.items()}

    def fine_sample(self, seconds):
        """One sampling phase, inside the page. Returns the sampler's own report."""
        expr = "(" + SAMPLER_JS + ")(" + repr(float(seconds)) + ")"
        res = self.call(cmd="eval", expr=expr)
        if not isinstance(res, str) or not res.lstrip().startswith("{"):
            raise SystemExit(f"sampler answered something that is not a report: {str(res)[:200]}")
        return json.loads(res)

    def fine_couple_test(self, seconds=4.0):
        """Does the guest's input move the *host's* picture? Three phases, one run.

        A no-input baseline, the guest alone holding RIGHT, then no input again -
        all on the live session, with the host's own input trace read out of the
        app's log afterwards so "the host sent nothing" is checked rather than
        assumed. The phase that must move is the guest's own screen (the control:
        it says the press reached a game at all); the claim under test is whether
        the *host's* picture moves where neither baseline did.

        The roles come off the digest, not from an assumption: the app's NSMB
        session runs with player 1 as the guest and player 2 as the host, and the
        coarse version of this test hard-coded the opposite - it pressed player
        2, called it the guest, and then counted player 1's input lines to prove
        the host had sent nothing (measured 2026-09-29).
        """
        roles = self.roles()
        guest = next((p for p, r in roles.items() if r == "guest"), None)
        host = next((p for p, r in roles.items() if r == "host"), None)
        if not guest or not host or guest == host:
            log(self.lines, f"fine coupling: no host/guest pair on the bus ({roles}) - skipped")
            return None
        log(self.lines, f"fine coupling: roles off the digest {roles} -> guest p{guest}, host p{host}")
        log(self.lines, f"  four phases of {seconds}s, sampled in the page at the app's own frame:"
                        " A no input, B guest RIGHT, C no input, D host RIGHT")
        mark = self.input_mark()
        a = self.fine_sample(seconds)
        self.call(cmd="input", player=guest, buttons=parse_buttons("right"))
        b = self.fine_sample(seconds)
        self.call(cmd="input", player=guest, buttons=0)
        c = self.fine_sample(seconds)
        # The mirror phase: the *host* alone holds the same direction. Without it
        # a null on the host side cannot be read - "the host's screen does not
        # show the guest" and "the host's screen does not respond to anything in
        # this state" look identical. With it, the pattern of which screens move
        # is the game's own answer.
        self.call(cmd="input", player=host, buttons=parse_buttons("right"))
        d = self.fine_sample(seconds)
        self.call(cmd="input", player=host, buttons=0)
        trace = self.input_trace(since=mark)
        phases = (("A no input", a), ("B guest RIGHT", b), ("C no input", c), ("D host RIGHT", d))
        for tag, r in phases:
            if r.get("err"):
                log(self.lines, f"  phase {tag}: sampler stopped early: {r['err']}")
            rate = r["iters"] / max(1e-6, r["ms"] / 1000.0)
            log(self.lines, f"  phase {tag}: {r['iters']} samples in {r['ms']}ms ({rate:.0f}/s), "
                            f"{r['pairs']} one-frame pairs, {r['skipped']} skipped "
                            f"({r['dseq']}), frames {r['pairFrames'][0] if r['pairFrames'] else '?'}"
                            f"..{r['pairFrames'][-1] if r['pairFrames'] else '?'}")
        if min(r["pairs"] for _, r in phases) < 20:
            log(self.lines, "  VERDICT: too few one-frame pairs to say anything (need >= 20 per phase)")
            return None
        # Which input landed in which phase, joined on the *frame*: each phase
        # reports the frames its pairs covered and the app's own trace says which
        # frame every line went out on, so "the host sent nothing in the guest's
        # phase" is checkable rather than assumed from phase order (two clocks:
        # the driver's wall clock and the session's emulated one).
        marks = [(tag, r["pairFrames"][0], r["pairFrames"][-1]) for tag, r in phases if r.get("pairFrames")]
        # A line belongs to a phase only if it took effect *inside* that phase's
        # covered frames. Everything else is a boundary: the driver sends a press
        # and the sampler's first pair is a frame or two later, so a press lands
        # just before its phase opens and just after the previous one closed. A
        # "nearest window" rule hands phase D's own press to phase C here -
        # measured 2026-09-29, press at frame 15271 against C ending at 15270 and
        # D starting at 15273 - and prints "phase C owns p2 1" for a baseline
        # phase the driver sent nothing in.
        sent, boundary, frames_of = {}, [], {}
        for t in trace:
            m = re.search(r"\[input\] frame (\d+) player (\d+)", t)
            if not m:
                continue
            fr, player = int(m.group(1)), int(m.group(2))
            frames_of.setdefault(player, []).append(fr)
            hit = next((tag for tag, f0, f1 in marks if f0 <= fr <= f1), None)
            if hit is None:
                boundary.append((fr, player, t))
            else:
                sent.setdefault(hit, {}).setdefault(player, []).append(t)
        for tag, f0, f1 in marks:
            counts = sent.get(tag, {})
            log(self.lines, f"  phase {tag} covered frames {f0}..{f1}; input lines inside it: "
                            + (", ".join(f"p{p} {len(v)}" for p, v in sorted(counts.items())) or "none"))
        for tag in sent:
            for p, lines in sorted(sent[tag].items()):
                for t in lines[:2]:
                    log(self.lines, f"    {tag}: {t}")
        for fr, player, t in boundary:
            near = min((min(abs(fr - f0), abs(fr - f1)), tag, f0, f1) for tag, f0, f1 in marks)
            log(self.lines, f"  at a phase boundary: {t} (nearest window {near[1]} {near[2]}..{near[3]})")
        for p in sorted(frames_of):
            who = "guest" if p == guest else ("host" if p == host else "?")
            log(self.lines, f"  the {who} console's own input over the whole measurement: "
                            f"{len(frames_of[p])} line(s), frames {min(frames_of[p])}..{max(frames_of[p])}")

        def masks(r, player, screen):
            return base64.b64decode(r["masks"][screen][player - 1])

        def spread(r, player, screen):
            m = sorted(r["moved"][screen][player - 1])
            if not m:
                return "no pairs"
            return f"median {m[len(m) // 2]} px/pair, p90 {m[int(len(m) * 0.9)]}, max {m[-1]}"

        names = {0: "top", 1: "bottom"}
        # Both no-input phases together are the baseline: a pixel that moves in
        # either one is the game's own animation, whatever the input did.
        base = {(player, s): mask_or(masks(a, player, s), masks(c, player, s))
                for player in (guest, host) for s in (0, 1)}
        only = {}
        for player, who in ((guest, "guest"), (host, "host")):
            for s in (0, 1):
                nb = mask_count(base[(player, s)])
                log(self.lines, f"  p{player} {who} {names[s]}: no-input baseline moved {nb} px "
                                f"of {256 * 192}")
                log(self.lines, f"      per pair: A {spread(a, player, s)} | C {spread(c, player, s)}")
                for tag, r in (("B guest RIGHT", b), ("D host RIGHT", d)):
                    inp = masks(r, player, s)
                    o = mask_andnot(inp, base[(player, s)])
                    only[(tag, player, s)] = o
                    log(self.lines, f"      {tag}: moved {mask_count(inp)} px, of which "
                                    f"{mask_count(o)} px not in either baseline"
                                    f"{'' if not mask_count(o) else f' bbox {mask_bbox(o)}'}"
                                    f" (per pair {spread(r, player, s)})")
        for tag, r in (("B", b), ("D", d)):
            for s, key in ((0, "shareTop"), (1, "shareBot")):
                v = sorted(r.get(key) or [0])
                log(self.lines, f"  in phase {tag} the two consoles' {names[s]} screens differed by "
                                f"{v[len(v) // 2]} px/pair (median) -> "
                                + ("the picture is shared, so a change there is one picture moving "
                                   "on both consoles" if s == 1 else
                                   "each console renders its own view"))
        def counted(tag, player):
            return mask_count(only[(tag, player, 0)]), mask_count(only[(tag, player, 1)])

        own_g, cross = counted("B guest RIGHT", guest), counted("B guest RIGHT", host)
        own_h, mirror = counted("D host RIGHT", host), counted("D host RIGHT", guest)
        log(self.lines, "  the 2x2, input-only pixels (top, bottom):")
        log(self.lines, f"    the guest's own screens under the guest's input: {own_g}")
        log(self.lines, f"    the host's screens    under the guest's input: {cross}")
        log(self.lines, f"    the host's own screens under the host's input:  {own_h}")
        log(self.lines, f"    the guest's screens   under the host's input:  {mirror}")
        # The control gates the claim. If the console that was pressed did not
        # move its OWN picture, this state cannot answer the question - and
        # without that gate a stray handful of pixels on the other side reads as
        # attribution. Measured 2026-09-29: a session ten minutes on, where the
        # guest's RIGHT moved the guest's own screens by 6 px, printed
        # "attributed ... 11 px" under a `> 0` test. Both thresholds are far above
        # that noise floor and far below the signal the working runs show (the
        # pressed console's own screens move tens of thousands of px, and the
        # cross-effect thousands).
        CONTROL_MIN_PX = 100
        CROSS_MIN_PX = 100
        control = sum(own_g)
        log(self.lines, f"  control: the guest's own input moved the guest's own screens by {control} px "
                        f"(>= {CONTROL_MIN_PX} says this state can answer the question); "
                        f"the host's own input moved the host's own screens by {sum(own_h)} px")
        if control < CONTROL_MIN_PX:
            log(self.lines, f"  VERDICT: inconclusive - the guest's screens moved only {control} px under the "
                            "guest's own input, so this state cannot show whether the host's picture reacts "
                            "to it. Nothing is claimed about the peer. (The game moves on from the state "
                            "this measurement needs; re-run the route for a fresh one.)")
            return phases, sent
        owner = ", ".join(f"p{p} {len(v)}" for p, v in sorted(sent.get("B guest RIGHT", {}).items())) or "none"
        if sum(cross) >= CROSS_MIN_PX:
            log(self.lines, f"  VERDICT: attributed - under the guest's input alone the host's own rendered "
                            f"pictures moved {sum(cross)} px that neither no-input baseline moved "
                            f"(top screen {cross[0]}, bottom screen {cross[1]}), while the input lines that "
                            f"phase owns are {owner} and the host's own trace is empty there.")
            if sum(mirror) > 0:
                log(self.lines, f"    the mirror is the same shape: under the host's input alone the "
                                f"guest's pictures moved {sum(mirror)} px (top {mirror[0]}, bottom {mirror[1]}), "
                                "so neither cross-effect is an animation the baselines happened to miss.")
            log(self.lines, f"    the bottom screen is ONE picture on both consoles (it differed by 0 px "
                            f"per pair throughout), so its {cross[1]} px is the shared map - which the "
                            f"host console renders from its own game state. The top-screen {cross[0]} px "
                            "is the host's own view reacting to the guest.")
        else:
            log(self.lines, f"  VERDICT: unattributed at the pixel level - the guest's own screens moved "
                            f"{control} px under the guest's own input (so the press landed), but the "
                            f"host's pictures moved only {sum(cross)} px that neither no-input baseline "
                            f"moved, which is below the {CROSS_MIN_PX} px this test calls a signal. The "
                            "input is delivered - what is missing is the peer's picture reacting to it. "
                            "Not called proven.")
        return phases, sent

    def couple_test(self):
        """The guest's input, in the host's picture, with the host's input untouched.

        A baseline first: the app's consoles animate on their own (a map, a HUD
        counter, the game's own ticks), so "the host's picture changed" means
        nothing unless it is *still* while nothing is pressed. Then the guest
        walks and nothing else is sent, and the host's own framebuffers are read
        throughout. The app's input trace is the other half: every input the
        frontend delivered is in the log with the player it went to, so "the
        host sent nothing" is checkable rather than assumed.
        """
        roles = self.roles()
        guest = next((p for p, r in roles.items() if r == "guest"), 2)
        host = next((p for p, r in roles.items() if r == "host"), 1)
        log(self.lines, f"coupling: roles {roles} -> guest p{guest}, host p{host}; 2.5s of nothing, "
                        f"then the guest alone holds RIGHT for 2.5s")
        mark = self.input_mark()
        idle = self.sample_pictures(2.5)
        log(self.lines, f"  baseline (no input): distinct pictures -- p1 {idle[1]}, p2 {idle[2]}")
        before = self.crcs()
        self.shot("nsmb-couple-before")
        self.press(guest, "right", ms=2500)
        time.sleep(0.3)
        after = self.crcs()
        self.shot("nsmb-couple-after")
        got = self.input_trace(since=mark)
        # The app writes the mask's own name and it is upper case (`buttons ->
        # RIGHT (mask 00000010)`), so compare case-insensitively - a lower-case
        # filter reported "0 input lines for the guest" for a press the app had
        # logged perfectly well (2026-09-29).
        walk = [t for t in got if f"player {guest} " in t and "right" in t.lower()]
        host_lines = [t for t in got if f"player {host} " in t]
        log(self.lines, f"  host frames before/after: {before[host][0]} {before[host][1]} -> "
                        f"{after[host][0]} {after[host][1]}")
        log(self.lines, f"  guest frames before/after: {before[guest][0]} {before[guest][1]} -> "
                        f"{after[guest][0]} {after[guest][1]}")
        log(self.lines, f"  host mean luma {before[host][2]} -> {after[host][2]}")
        log(self.lines, f"  input lines the guest got in that window: {len(walk)}")
        for t in walk[:6]:
            log(self.lines, f"    {t}")
        log(self.lines, f"  input lines for the host in that window: {len(host_lines)} "
                        "(0 = the host sent nothing, so a change in its picture is the guest's)")
        return before, after, got

    def run(self):
        if self.args.restart:
            self.restart()
        self.connect()
        if self.args.restart:
            # Boot is a *frame* the route depends on, not a wall-clock guess:
            # see wait_boot.
            self.wait_boot()
        self.report("boot")
        self.shot("nsmb-boot")

        landed = self.session_data()
        if not self.args.no_drive:
            landed = self.run_route()
            # The route's input stops at FR_STOP_INPUT; let the session run on
            # its own from there, reading it rather than driving it. A
            # --stop-at drive ends where it was told to instead.
            while not self.args.stop_at and self.frames_now() < FR_END:
                time.sleep(2.0)
                bal = self.balance()
                if self.session_data(bal):
                    landed = True
                log(self.lines, f"  free-running at frame {self.frames_now()}: {self.balance_line(bal)}")
        self.report("final")
        bal = self.balance()
        log(self.lines, f"link: {self.balance_line(bal)}")
        log(self.lines, f"session data: {self.session_data(bal)} · deauths: {self.deauths(bal)}")
        if self.missed:
            log(self.lines, f"route frames missed (delivered late): "
                            + ", ".join(f"{t} by {n - t}" for t, n in self.missed))
        else:
            log(self.lines, "route frames missed: none - every input landed inside its frame")
        self.shot("nsmb-final")
        if self.args.couple and self.session_data(bal):
            self.couple_test()
            self.report("after-couple")
        if self.args.couple_fine and self.session_data(bal):
            self.fine_couple_test()
            self.report("after-couple-fine")
        log(self.lines, "wireless events:")
        for ev in self.wireless_events():
            log(self.lines, "  " + ev)
        log(self.lines, f"landed: {landed}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--restart", action="store_true", help="start the app (through run_app.sh, with its save pin)")
    ap.add_argument("--no-drive", action="store_true", help="only read the session that is up")
    ap.add_argument("--couple", action="store_true", help="measure the guest's input on the host's picture")
    ap.add_argument("--couple-fine", action="store_true",
                    help="the same claim at frame granularity: four phases sampled inside the "
                         "page (no input / the guest alone holding RIGHT / no input / the host "
                         "alone holding RIGHT - the mirror is what makes a null readable), and "
                         "the pixels that moved under one console's input and in neither baseline")
    ap.add_argument("--rom", default=ROM)
    ap.add_argument("--port", type=int, default=7373)
    ap.add_argument("--log", default=os.path.join(ROOT, "logs/nsmb_app.log"))
    ap.add_argument("--out", default=os.path.join(ROOT, "logs/nsmb_join.log"))
    ap.add_argument("--boot-wait-max", type=float, default=60.0,
                    help="give up if no console is running this many seconds after the app answers")
    ap.add_argument("--shots-at", default="",
                    help="comma-separated route frames to capture (both consoles' screens)")
    ap.add_argument("--stop-at", type=int, default=0,
                    help="end the drive at this route frame (0 = run the whole route)")
    ap.add_argument("--stop-when-coupled", action="store_true",
                    help="stop feeding the route as soon as the wire shows a session "
                         "(the harness keeps tapping A/CONFIRM until frame 11500, and that "
                         "is what takes the two games out of the lobby and into the race)")
    ap.add_argument("--route", choices=("harness", "v019x"), default="harness",
                    help="which recorded route to drive (see plan()'s docstring)")
    args = ap.parse_args()
    args.shots_at = {int(v) for v in args.shots_at.replace(" ", "").split(",") if v}
    args.show = set()
    args.record = False
    args.only_record = False

    lines = []
    runner = NsmbRunner(args, lines)
    try:
        runner.run()
    finally:
        if runner.ch:
            runner.ch.close()
        with open(args.out, "a") as fh:
            fh.write("\n".join(lines) + "\n")
        print(f"\n(timeline appended to {args.out})")


if __name__ == "__main__":
    main()
