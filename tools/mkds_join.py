#!/usr/bin/env python3
"""Hands-free Mario Kart DS local-play join, driven through the control channel.

One iteration of the loop this project lives in: boot both consoles, walk MKDS's
menus, get P2 into P1's room, then close the lobby and watch what happens on the
wire and on both screens. Prints a compact report so each iteration reads in a
few dozen lines, and saves screenshots for anything that needs human eyes.

    tools/mkds_join.py --restart                 # drive to the group list and look
    tools/mkds_join.py --no-restart --host-room  # P1 creates the room, P2 joins
    # the standard hands-free verification (v0.1.8v): join, close the lobby,
    # pick a game, and hold the link for 25s
    tools/mkds_join.py --restart --no-dialogs --cut-off --vs --watch 25
    tools/mkds_join.py --no-drive --watch 30     # watch a session someone else drove
    tools/mkds_join.py --record                  # log a human's touches instead

    **--no-dialogs for this save.** MKDS's first-time nickname/emblem dialogs are
    consumed by the save file after the first join, so the default `--dialogs 3`
    now taps into the group list and turns P2 into its own host. If a run reports
    `p1=host p2=host`, that is what happened.

The route is the one measured off captured frames in history.md v0.1.7 / v0.1.8
("Driving a real game's menus"), not guessed. Each step names its coordinate
constant from the block below, which is the only copy of the numbers - the
docstring used to spell them out too, and the two had to be kept in step by
hand:

  1. title screen: tap TITLE (its centre) - it does not answer START
  2. main menu: MULTIPLAYER
  3. three confirm dialogs (nickname, showing your name, emblem): DIALOG_YES
  4. group list: CREATE_GROUP, a group bar at the top at JOIN_GROUP
  5. mode select (after CREATE GROUP, not before): NORMAL, or SIMPLE one tap
     away - and the wrong one still reaches *a* lobby
  6. host: CUT_OFF, then VS on the game-select screen

Two of those coordinates were re-measured off the emulated screens on
2026-09-28 and both are real controls:

  * On the mode-select screen NORMAL's own block is y 24..81 of the bottom
    screen (its label bar y 24..40, the description box below), so NORMAL's
    point is inside it.
  * CUT OFF only exists once a second racer has joined: at 1/8 the bottom
    screen has no such button at all, and at 2/8 it is the light button at
    x 84..214, y 156..187 with CUT_OFF inside it. The host's own top screen
    says "Touch Cut Off to stop accepting entries and begin racing", and the
    host's next command frames carry 00 FF / 00 FE - so it is MKDS's leave-the-
    lobby/begin control, not a stray tap.
  * On the game-select screen VS is the top block (y 6..45, label at y 21..33),
    so VS hits it.

Every tap goes through wait_stable() first, because MKDS's title screen is an
attract loop: it plays a demo race and comes back to the menu by itself, and a
tap that lands while the demo is up is silently discarded. Measured on
2026-09-28: two route taps in a row were eaten by the demo, and the run then
read as "the game ignored the tap" - the single most misleading failure mode
this tool has. The title step is the one place the wait can time out (the demo
animates the touch screen for its whole loop); MKDS reaches its own menu without
a press - P2 was never pressed at the title and still listed P1's group - so a
timed-out title tap costs a few seconds, not correctness, and the step after it
waits for the menu properly.

The two presses that decide whether a race is reachable are the exception to the
clock, in both directions. CUT OFF and VS do not exist for long, so they are
pressed on the *screen*: CUT_OFF_RECT is sampled through the control channel
(probe) and the tap fires the moment a second racer's join changes that patch,
and VS fires when the game-select screen replaces the lobby. The baseline for
the CUT OFF patch is taken while the host's room holds one racer, so the trigger
is a measured change rather than a brightness nobody has calibrated. This is not
fussy: on 2026-09-28 a fixed settle plus a settle-wait put the CUT OFF press
about ten seconds after the join, by which time MKDS had ended the session.

Every step prints a screen hash, the per-console role, link state, picture state
(changing/frozen) and the arm counters, so "the tap did nothing", "the screen
changed but nobody joined", "the guest's game stopped arming" and "one game's
picture froze" are all distinguishable without reading any pixels.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from appctl import Channel, parse_buttons  # noqa: E402  (the control-channel client)

TITLE = (128, 96)
MULTIPLAYER = (128, 68)
DIALOG_YES = (67, 145)
CREATE_GROUP = (128, 157)
NORMAL = (128, 68)
SIMPLE = (67, 145)
JOIN_GROUP = (127, 23)
CUT_OFF = (149, 167)
VS = (128, 22)

# The patch of the host's touch screen that CUT OFF appears in, and that is
# empty blue until a second racer has joined (x 84..214, y 156..187, so the
# rectangle is that block plus a margin). The press is aimed off this: the
# driver watches its brightness and taps the instant it changes, instead of
# pressing on a clock and hoping the button was up (see wait_for_offer).
CUT_OFF_RECT = (84, 151, 132, 41)

# Injected into the page by --record: every input the frontend sends reaches the
# log as [ui] input {...}, which turns a human walkthrough into a replayable
# route with the exact DS coordinates and button masks (tools/appctl.py eval).
RECORD_JS = """
(function () {
  const core = window.__TAURI__.core;
  if (core.__mdRecorder) return 'already recording';
  const orig = core.invoke;
  core.invoke = function (cmd, args) {
    if (cmd === 'set_input' && args && args.input) {
      orig('frontend_log', { level: 'info', message: 'input ' + JSON.stringify(args.input) });
    }
    return orig.apply(core, arguments);
  };
  core.__mdRecorder = true;
  return 'recording inputs';
})()
"""


def log(lines, text):
    stamp = time.strftime("%H:%M:%S")
    for ln in str(text).splitlines():
        lines.append(f"{stamp} {ln}")
    print(text, flush=True)


class Runner:
    def __init__(self, args, lines):
        self.args = args
        self.lines = lines
        self.log_path = args.log
        self.ch = None

    # ---- app + channel -------------------------------------------------

    def restart(self):
        env = dict(os.environ)
        env.update(
            MD_AUTOSTART_PLAYERS="2",
            MD_AUTOSTART_ROM=self.args.rom,
            MD_LINK_VERBOSE=os.environ.get("MD_LINK_VERBOSE", "1"),
            MD_CMD_RETRY_NOTIFY=os.environ.get("MD_CMD_RETRY_NOTIFY", "1"),
            RUST_LOG="info",
            MD_LOG=self.log_path,
            MD_CONTROL_PORT=str(self.args.port),
        )
        out = subprocess.run(
            [os.path.join(HERE, "run_app.sh")], env=env, cwd=ROOT, capture_output=True, text=True
        )
        for line in out.stdout.splitlines():
            if line.startswith(("launched", "control:", "autostart")):
                log(self.lines, line)
        if out.returncode != 0:
            raise SystemExit(f"run_app.sh failed: {out.stderr.strip()}")

    def connect(self, timeout=45):
        start = time.time()
        while time.time() - start < timeout:
            try:
                self.ch = Channel(self.args.port, timeout=30)
                self.ch.call(cmd="ping")
                return
            except SystemExit:
                time.sleep(0.5)
        raise SystemExit("control channel never came up")

    def call(self, **req):
        return self.ch.call(**req)

    # ---- observation ---------------------------------------------------

    def frames(self):
        st = self.call(cmd="status")
        if not st.get("active"):
            return None
        return {f["player"]: f["seq"] for f in st.get("frames", [])}

    # Field name, digest pattern. Every field is named here; a positional
    # zip() used to drop whatever did not have a name of its own, and `canned`
    # then read as a confident zero.
    # Every digest field names its own pattern and its own group order, so a field
    # can never be read from the wrong position. That is the whole point: a
    # caller that matched the line once and indexed the groups positionally read
    # the app's host as collecting nothing for a whole pass (`collected 0 empty
    # 1021`) when the digest said `collected 1021(+298) empty 4(+4)` - one group
    # off from `canned` onward, so `replies sent 0` was read as `collected 0` and
    # `collected 1021` as `empty 1021` (2026-09-29, tools/nsmb_join.py). A wrong
    # number that looks like a plausible measurement is worse than a missing one.
    DIGEST_FIELDS = (
        (("deauth_out", "deauth_in"), r"deauth out (\d+) in (\d+)"),
        (("answered", "answered_delta", "blank", "canned"),
         r"answered (\d+)\(\+(\d+), blank (\d+), canned (\d+)\)"),
        (("cmds_rx", "cmds_rx_delta"), r"cmds rx (\d+)\(\+(\d+)"),
        (("arms", "aids"), r"arms (\d+) aids (\d+)"),
        (("window_reads", "window_reads_delta"), r"window reads (\d+)\(\+(\d+)\)"),
        (("replies_sent",), r"replies sent (\d+)"),
        (("collected", "collected_delta"), r"collected (\d+)\(\+(\d+)\)"),
        (("empty", "empty_delta"), r"empty (\d+)\(\+(\d+)\)"),
        (("stale",), r"stale (\d+)"),
        (("reanchor_dropped",), r"reanchor-dropped (\d+)"),
        (("blank_replies",), r"blank (\d+) notlooking"),
        (("notlooking",), r"notlooking (\d+)"),
        (("cmds_tx", "cmds_tx_delta"), r"cmds tx (\d+)\(\+(\d+)"),
        (("acks", "acks_delta"), r"acks (\d+)\(\+(\d+)"),
        (("clientfail",), r"clientfail (\d+)"),
    )

    @classmethod
    def parse_digest(cls, text):
        """The digest line's fields, by name. See DIGEST_FIELDS."""
        out = {}
        for names, pat in cls.DIGEST_FIELDS:
            hit = re.search(pat, text)
            if hit:
                out.update(dict(zip(names, (int(v) for v in hit.groups()))))
        return out
    HEALTH_FIELDS = (
        (("link",), r"health p\d: LINK (\w+)"),
        # `still` is what the app calls a picture that has held (it was `FROZEN`
        # until v0.1.9ae renamed it, because a menu and a dialog hold one image
        # too). This driver went on reading only `changing`/`FROZEN` and so read
        # a held picture as "no picture state in the log at all" - a driver
        # defect with no symptom except a field quietly missing from every
        # report.
        (("picture",), r"health p\d: .*?picture (changing|still|FROZEN) \(([^)]*)\)"),
        (("pc_mobility",), r"health p\d: .*?pc (\d+)/100"),
    )

    def mp_state(self, tail=60000):
        """Newest multiplayer digest and health line per console, parsed.

        The window is a line count and the log bursts: the emulation threads
        emit tens of thousands of lines in a couple of seconds around a
        transition, so a small tail can hold no health line at all and the
        link/picture fields then read as absent rather than as stale.

        The role is not always host or guest either - a console whose link has
        gone says `[not in MP]`, and a parse that only knew the two word roles
        used to drop that line entirely, so a console that had left the group
        looked to the driver like a console with no opinion.
        """
        state = {}
        try:
            with open(self.log_path, errors="replace") as fh:
                lines = fh.readlines()[-tail:]
        except OSError:
            return state
        for ln in lines:
            m = re.search(r"mp p(\d) \[([^\]]*)\]: (.*)", ln)
            if m:
                row = state.setdefault(int(m.group(1)), {})
                row["role"] = m.group(2)
                row.update(self.parse_digest(m.group(3)))
            last = re.search(
                r"mp p(\d) last: cmd seq ([0-9A-F]+) len (\d+) body ([0-9A-F ]*?) at (\+[\d.]+s)"
                r" · answer len (\d+) body (.*?) at",
                ln,
            )
            if last:
                row = state.setdefault(int(last.group(1)), {})
                row["last_cmd"] = f"seq {last.group(2)} len {last.group(3)} body {last.group(4).strip()} at {last.group(5)}"
                row["last_answer"] = f"len {last.group(6)} body {last.group(7).strip()}"
            # The interrupt-level lines the digest now prints: kept whole, because
            # the point of them is the reader seeing which source is flat while a
            # bit sits pending, and any field-narrowing parse would hide that.
            m = re.search(r"mp p(\d) irq: (.*)", ln)
            if m:
                state.setdefault(int(m.group(1)), {})["irq"] = m.group(2).strip()
            m = re.search(r"mp p(\d) hw: (.*)", ln)
            if m:
                state.setdefault(int(m.group(1)), {})["hw"] = m.group(2).strip()
            h = re.search(r"health p(\d):", ln)
            if h and "mp p" not in ln:
                row = state.setdefault(int(h.group(1)), {})
                for names, pat in self.HEALTH_FIELDS:
                    hit = re.search(pat, ln)
                    if hit:
                        if names[0] == "picture":
                            row["picture"] = f"{hit.group(1)} ({hit.group(2)})"
                        else:
                            row[names[0]] = int(hit.group(1)) if hit.group(1).isdigit() else hit.group(1)
        return state

    def roles(self):
        return {p: s["role"] for p, s in self.mp_state().items() if "role" in s}

    def health_line(self):
        """One compact per-console line: role, link, picture, arm counters."""
        out = []
        for p, s in sorted(self.mp_state().items()):
            bits = [f"p{p}={s.get('role', '?')}"]
            for key in ("link", "picture"):
                if key in s:
                    bits.append(str(s[key]))
            if "arms" in s:
                bits.append(f"arms {s['arms']} answered {s.get('answered', 0)} "
                            f"blank {s.get('blank', 0)} canned {s.get('canned', 0)}")
            if "collected" in s:
                bits.append(f"collected {s['collected']} empty {s.get('blank_replies', 0)} "
                            f"clientfail {s.get('clientfail', 0)}")
            if "irq" in s:
                bits.append(f"irq {s['irq']}")
            if "last_cmd" in s:
                bits.append(f"handed {s['last_cmd']}")
            if "last_answer" in s:
                bits.append(f"wrote back {s['last_answer']}")
            out.append(" ".join(bits))
        return " · ".join(out)
    def wireless_events(self, tail=600):
        out = []
        try:
            with open(self.log_path, errors="replace") as fh:
                lines = fh.readlines()[-tail:]
        except OSError:
            return out
        for ln in lines:
            if "[link-event]" in ln and ("wireless" in ln or "WIFI" in ln):
                out.append(ln.strip()[len("[link-event] "):] if "[link-event]" in ln else ln.strip())
        return out[-8:]

    def screen(self, player, screen="bottom", cols=64, crop=None, aspect=0.5):
        # The screen goes over the wire by name, and the channel takes names as
        # well as numbers; the hash covers the screen that was asked for and
        # nothing else (it used to cover both, because the name did not parse).
        txt = self.call(cmd="see", player=player, screen=screen, cols=cols, crop=crop, aspect=aspect)
        body = "\n".join(txt.splitlines()[1:])
        return hashlib.md5(body.encode()).hexdigest()[:8], txt

    def probe(self, player, rect):
        """Mean luma (0-255) of a rectangle of a console's touch screen.

        Reads the same frames the frontend draws, through the control
        channel's eval, so a trigger can watch a control rather than a clock.
        """
        x, y, w, h = rect
        js = (
            "(async () => { const b = await window.__mdctl.frames(); if (!b) return -1;"
            " const p = window.__mdctl.unpack(b);"
            f" const o = p.at({player - 1}) + window.__mdctl.BPS, px = p.u8;"
            " let s = 0, n = 0;"
            f" for (let yy = {y}; yy < {y + h}; yy++)"
            f"  for (let xx = {x}; xx < {x + w}; xx++) {{"
            "   const i = o + (yy * 256 + xx) * 4;"
            "   s += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]; n++; }"
            " return n ? s / n : -1; })()"
        )
        return float(self.call(cmd="eval", expr=js))

    def report(self, tag, show=False):
        frames = self.frames() or {}
        h1, _ = self.screen(1)
        h2, _ = self.screen(2)
        log(self.lines, f"[{tag}] frames {frames} · screens p1:{h1} p2:{h2}")
        log(self.lines, f"  {self.health_line() or 'no mp activity yet'}")
        for p in (1, 2):
            log(self.lines, f"  {self.screen_truth(p)}")
        for p in (1, 2):
            log(self.lines, f"  {self.irq_truth(p)}")
        for p in (1, 2):
            log(self.lines, f"  {self.io_truth(p)}")
        if show:
            for p in (1, 2):
                _, txt = self.screen(p)
                log(self.lines, f"--- {tag}: player {p} bottom ---\n{txt}")

    def screen_truth(self, player):
        """One line: the emulator's own picture and the registers behind it.

        `see` renders the copy the frame pump took, so a black screen there has
        three possible authors - the game drawing black, the game turning the
        screens off, or our copy path. The channel's probe reads the renderer's
        framebuffers directly and the POWCNT1 bits that decide what a screen
        shows, so the step report can tell them apart without a human deciding
        whether a black window means a crash.
        """
        try:
            p = self.call(cmd="probe", player=player)
        except SystemExit as err:
            return f"p{player} probe refused: {err}"
        fb, cp = p["framebuffer"], p["copy"]
        same = "same" if (fb["top"]["crc"] == cp["top"]["crc"] and
                          fb["bottom"]["crc"] == cp["bottom"]["crc"]) else "DIFFERENT"
        flags = ",".join(
            name for name, on in (("lcd", p["screens_enabled"]), ("A", p["engine_a"]),
                                  ("B", p["engine_b"]), ("swap", p["screen_swap"])) if on
        ) or "none"
        return (f"p{player} framebuffer top {fb['top']['mean']} ({fb['top']['black_pct']}% black) "
                f"bottom {fb['bottom']['mean']} ({fb['bottom']['black_pct']}% black) · "
                f"copy {same} · powcnt1 {p['powcnt1']} [{flags}] · slept {'yes' if p['asleep'] else 'no'} · "
                f"picture changed {cp['beats']}x, last {cp['last_beat_frame']} frames ago")

    def wifi_io(self, player):
        """The console's own reads of the wireless block, as text.

        A value the emulator fabricates and a value the peer's game wrote are
        identical in every counter taken from the write side or the wire; the
        only place they differ is where the game's bytecode reads them. So this
        is the raw material for "what value did this game branch on": the
        registers it reads with counts and last values, the last few hundred
        reads in order, and every halfword it consumed out of the frame it was
        handed.
        """
        try:
            return self.call(cmd="wifiio", player=player)
        except SystemExit as err:
            return f"p{player} wifiio refused: {err}"

    def io_truth(self, player):
        """One line: what this game is reading, and out of which frame."""
        txt = self.wifi_io(player)
        if txt.startswith(f"p{player} wifiio refused"):
            return txt
        handed = next((ln.strip() for ln in txt.splitlines() if "handed:" in ln), "handed: ?")
        reads = []
        for ln in txt.splitlines():
            m = re.match(r"\s+([0-9A-F]{3}) (\S+)\s+(\d+)\s+([0-9A-F]{4})$", ln)
            if m:
                reads.append(f"{m.group(2)}({m.group(3)}x)={m.group(4)}")
        order = []
        grab = False
        for ln in txt.splitlines():
            if "read order, oldest first" in ln:
                grab = True
                continue
            if grab and ln.startswith("   "):
                order.append(ln.strip())
            elif grab:
                break
        return (f"p{player} io: {handed} · reads {', '.join(reads[:10]) or 'none'}"
                f" · consumed {' '.join(order)[:400] or 'nothing'}")

    def irq_truth(self, player):
        """One line: is this console's game still servicing the wifi IRQ, and
        what is it waiting on?

        A black picture with the CPUs still moving is what a game waiting on an
        interrupt it never receives looks like, and the byte counters cannot
        separate that from a game that is simply idle. `raised` is every time
        the wifi block asked this CPU for attention on that source, `acked`
        every time the game cleared it; a handler that still runs keeps both
        climbing, and the source listed with a flat zero next to a nonzero
        `pending` is the one it is waiting for. The PCs say whether either CPU
        has stopped executing at all.
        """
        try:
            p = self.call(cmd="probe", player=player)
        except SystemExit as err:
            return f"p{player} irq probe refused: {err}"
        w = p.get("wifi")
        if not w:
            return f"p{player} irq: no wifi block in the probe"
        role = {0: "none", 1: "host", 2: "guest"}.get(w["role"], w["role"])
        return (f"p{player} irq [{role}]: cpu line {w['irq_cpu_line']} raised {w['raised']} "
                f"· acked {w['acked']} · IF {w['w_if']} IE {w['w_ie']} pending {w['w_pending']} "
                f"· arm7 IME {w['arm7_ime']} IE7 {w['arm7_ie7']} IF7 {w['arm7_if7']} "
                f"(wifi ie {'yes' if w['wifi_ie_bit'] else 'no'}, "
                f"if {'yes' if w['wifi_if_bit'] else 'no'}) "
                f"· arm/answered/canned {w['arms']}/{w['answered']}/{w['canned']} "
                f"cmds rx/tx {w['cmds_rx']}/{w['cmds_tx']} "
                f"· us cntl {w['us_cntl']} at {w['us_at']} "
                f"bcn-irq14-blocked {'yes' if w['beacon_irq14_blocked'] else 'no'} "
                f"bcn {w['bcn_count']} · slots {w['txslots']} "
                f"txbusy {w['txbusy']} req {w['txreqread']} com {w['comstatus']} "
                f"pwr {w['powerstate']}/{w['powertx']} · pc9 {p.get('pc9', '?')} "
                f"pc7 {p.get('pc7', '?')} frames {p.get('frames', '?')}")

    # ---- acting --------------------------------------------------------

    def tap(self, player, xy, ms=180, settle=True):
        # A tap only counts if the screen it is aimed at is up and holding
        # still: MKDS answers touches during its attract demo with silence, so
        # without this wait a swallowed tap is indistinguishable from a screen
        # that ignored the player. (See the module docstring.)
        if settle:
            if not self.wait_stable(player):
                log(self.lines, f"  (player {player}'s screen never settled - tapping anyway)")
        self.call(cmd="touch", player=player, x=xy[0], y=xy[1], ms=ms)

    def wait_for_offer(self, player, xy, rect, label, base=None, timeout=None):
        """Tap `xy` the moment the control inside `rect` appears.

        Event-driven on the host's own screen: CUT OFF only exists once a
        second racer has joined, and pressing late is not a slow press but a
        missed one - MKDS ends the session on its own within seconds (2026-09-
        28: the driver's fixed settle plus a settle-wait on an animating
        screen put the press ~10s after the join). `base` is the luma of the
        same patch taken before the offer could exist, so the trigger is a
        change, not a brightness threshold nobody has measured.
        """
        timeout = self.args.offer_timeout if timeout is None else timeout
        want = self.args.offer_delta
        start = time.time()
        samples = 0
        seen = base
        while time.time() - start < timeout:
            lum = self.probe(player, rect)
            samples += 1
            if lum < 0:
                log(self.lines, "  (no frames to read: the console has no picture to probe)")
                break
            if seen is None or seen < 0:
                seen = lum
            if abs(lum - seen) >= want:
                took = time.time() - start
                log(self.lines, f"  {label} on p{player}'s screen after {took:.1f}s "
                                f"(patch luma {seen:.0f} -> {lum:.0f}), tapping {xy}")
                self.call(cmd="touch", player=player, x=xy[0], y=xy[1], ms=180)
                return True
            time.sleep(self.args.offer_interval)
        log(self.lines, f"  {label} never changed p{player}'s screen patch (luma "
                        f"{seen:.0f} over {samples} samples, {timeout:.0f}s), tapping {xy} anyway")
        self.call(cmd="touch", player=player, x=xy[0], y=xy[1], ms=180)
        return False

    def wait_for_change(self, player, before, label, timeout=20.0, screen="bottom"):
        """Wait until a console's touch screen stops showing what it showed."""
        start = time.time()
        while time.time() - start < timeout:
            h, _ = self.screen(player, screen=screen)
            if h != before:
                log(self.lines, f"  {label}: p{player}'s {screen} screen left {before} "
                                f"for {h} after {time.time() - start:.1f}s")
                return h
            time.sleep(0.3)
        log(self.lines, f"  {label}: p{player}'s {screen} screen still {before} after {timeout:.0f}s")
        return None

    def wait_stable(self, player, timeout=25.0):
        """Wait for the target console's touch screen to stop changing.

        The touch screen is the right thing to watch: MKDS's attract demo
        animates it continuously, while every menu, the group list, the lobby
        and the game-select screen hold it still even when the top screen is
        still animating (the lobby's "SEEKING OPPONENTS..." does exactly that).
        """
        start = time.time()
        last = None
        while time.time() - start < timeout:
            h, _ = self.screen(player, screen="bottom")
            if h == last:
                return True
            last = h
            time.sleep(0.4)
        return False

    def press(self, player, buttons, ms=180):
        # Through the client's own name table, because the channel takes a
        # button *mask*: `lib.rs`'s `press` reads `buttons` as a u64, so a name
        # sent straight down the wire became 0 - the app held nothing, answered
        # `held buttons 0x0 on player 1 for 400ms` and wrote no `[input]` line
        # at all. Measured on 2026-09-29 with `--press-a` and with the park
        # battery of `tools/scripts/mkds-park-probe.py`: 0 `buttons ->` lines
        # for 4 presses. `appctl.py` owns the vocabulary; ask it, and let it
        # refuse a name nothing answers to.
        got = self.call(cmd="press", player=player, buttons=parse_buttons(buttons), ms=ms)
        # The app answers with what it held (`held buttons 0x1 on player 1 for
        # 120ms`). The caller used to drop that string on the floor, so a press
        # that held nothing read as success - which is how the mask bug above
        # stayed invisible. Say it instead.
        held = re.search(r"held buttons 0x([0-9A-Fa-f]+)", str(got))
        if held and int(held.group(1), 16) == 0:
            log(self.lines, f"  WARNING: press {buttons} on player {player} held nothing ({got})")
        return got

    def shot(self, name):
        try:
            out = self.call(cmd="shot", name=name)
            log(self.lines, f"screenshot {out['path']}")
        except SystemExit as err:
            log(self.lines, f"screenshot failed: {err}")

    def wait_for_role(self, role, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if role in self.roles().values():
                return time.time() < deadline
            time.sleep(0.4)
        return False

    # ---- the route -----------------------------------------------------

    def walk(self, player, label, stop_at_group_list):
        """Steps 1-3 (and 5) for one console, reporting after each tap."""
        steps = [
            ("title", TITLE, self.args.title_wait),
            ("multiplayer", MULTIPLAYER, self.args.menu_wait),
        ]
        if not self.args.no_dialogs:
            for i in range(self.args.dialogs):
                steps.append((f"dialog{i + 1}-yes", DIALOG_YES, self.args.dialog_wait))
        if not stop_at_group_list and not self.args.joiner:
            steps.append(("create-group", CREATE_GROUP, self.args.menu_wait))
            steps.append(("normal", NORMAL, self.args.menu_wait))
        for name, xy, wait in steps:
            # One line before the tap, not only the report after it. A route that
            # is killed or wedged during a step leaves nothing behind otherwise,
            # and "which console's walk got to which tap" is the first question
            # asked of a run that never joined (2026-09-28: a run's output
            # stopped at the previous console's room shot with no traceback and
            # no way to tell whether it had reached the second console at all).
            log(self.lines, f"{label}-{name}: tapping {xy} on player {player}")
            self.tap(player, xy)
            time.sleep(wait)
            self.report(f"{label}-{name}", show=f"{label}-{name}" in self.args.show)
        if not stop_at_group_list and not self.args.joiner:
            self.shot(f"{label}-room")

    def run(self):
        if self.args.restart:
            self.restart()
        self.connect()
        if self.args.record:
            log(self.lines, "recorder: " + str(self.call(cmd="eval", expr=RECORD_JS)))
        if self.args.restart:
            log(self.lines, f"waiting {self.args.boot_wait}s for boot")
            time.sleep(self.args.boot_wait)
        self.report("boot", show="boot" in self.args.show)
        if self.args.only_record:
            return

        if not self.args.no_drive:
            # P1 first: its room must exist before P2's list is read.
            self.walk(1, "p1", stop_at_group_list=False)
            if self.args.joiner:
                log(self.lines, "P1 is the host this run; only driving P2")
            self.walk(2, "p2", stop_at_group_list=True)

            # P1 should now be sitting in its own room; P2 is scanning.
            if not self.wait_for_role("host", self.args.host_wait):
                log(self.lines, "no host role yet - P1's room screen may want another tap")
                self.tap(1, CREATE_GROUP)
                time.sleep(2.5)
                self.report("p1-create-again", show="p1-create-again" in self.args.show)

            # The offer's baseline is the host's own lobby with one racer in
            # it: the patch is empty until P2's join puts CUT OFF there, so
            # this sample is taken before the join can have landed.
            offer_base = None
            if "guest" not in self.roles().values():
                try:
                    offer_base = self.probe(1, CUT_OFF_RECT)
                    log(self.lines, f"host's CUT OFF patch reads {offer_base:.0f} with one racer")
                except SystemExit as err:
                    log(self.lines, f"cannot probe the host's screen: {err}")
                for attempt in range(self.args.join_attempts):
                    log(self.lines, f"P2 taps the group bar at {JOIN_GROUP}")
                    self.tap(2, JOIN_GROUP)
                    if self.wait_for_role("guest", self.args.join_wait / max(1, self.args.join_attempts)):
                        break
                    self.report(f"join-attempt-{attempt + 1}", show=f"join-attempt-{attempt + 1}" in self.args.show)
        self.report("lobby", show="lobby" in self.args.show)
        self.shot("mkds-lobby")

        if self.args.cut_off:
            # No settle and no stability wait: the press is aimed at the offer
            # itself, which is the only moment that exists.
            log(self.lines, f"P1 waits for the CUT OFF offer on its own screen, then taps {CUT_OFF}")
            self.wait_for_offer(1, CUT_OFF, CUT_OFF_RECT, "CUT OFF", base=offer_base)
            time.sleep(self.args.settle + self.args.step_wait)
            self.report("after-cut-off", show="after-cut-off" in self.args.show)
            self.shot("mkds-after-cut-off")
        if self.args.vs:
            # VS exists on the game-select screen, which replaces the lobby: the
            # trigger is that replacement, on the touch screen where VS lives.
            before, _ = self.screen(1, screen="bottom")
            self.wait_for_change(1, before, "before VS", timeout=self.args.vs_timeout)
            log(self.lines, f"P1 taps VS {VS}")
            self.tap(1, VS, settle=False)
            time.sleep(self.args.step_wait)
            self.report("after-vs", show="after-vs" in self.args.show)
            self.shot("mkds-after-vs")
        if self.args.press_a:
            log(self.lines, "P1 presses A")
            self.press(1, "a")
            time.sleep(self.args.step_wait)
            self.report("after-a", show="after-a" in self.args.show)

        if self.args.watch:
            end = time.time() + self.args.watch
            while time.time() < end:
                time.sleep(2.0)
                self.report("watch", show=False)
            self.shot("mkds-watch-end")

        log(self.lines, "wireless events:")
        for ev in self.wireless_events():
            log(self.lines, "  " + ev)
        self.report("final", show="final" in self.args.show)

        # The full read report for both consoles, at the state the run ended
        # in: the per-report line above is a summary, and this is the raw
        # material it summarises (every register read with its last value, the
        # last few hundred reads in order, and the halfwords each game took out
        # of the frame it was handed, in order).
        for p in (1, 2):
            log(self.lines, f"--- wifi reads player {p} ---\n{self.wifi_io(p)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--restart", action="store_true")
    ap.add_argument("--no-drive", action="store_true", help="only watch/report, change nothing")
    ap.add_argument("--record", action="store_true", help="log the frontend's own inputs (for a human run)")
    ap.add_argument("--only-record", action="store_true", help="start the recorder and return")
    ap.add_argument("--joiner", action="store_true", help="P1's room already exists: drive P2 only")
    ap.add_argument("--no-dialogs", action="store_true")
    ap.add_argument("--rom", default=os.path.join(ROOT, "Test Roms/0168 - Mario Kart DS (U)(SCZ).nds"))
    ap.add_argument("--port", type=int, default=7373)
    ap.add_argument("--log", default=os.path.join(ROOT, "logs/md_app.log"))
    ap.add_argument("--out", default=os.path.join(ROOT, "logs/mkds_join.log"))
    ap.add_argument("--boot-wait", type=float, default=20.0)
    ap.add_argument("--title-wait", type=float, default=2.5)
    ap.add_argument("--menu-wait", type=float, default=3.0)
    ap.add_argument("--dialog-wait", type=float, default=2.5)
    ap.add_argument("--dialogs", type=int, default=3, help="confirm dialogs to answer with Yes")
    ap.add_argument("--host-wait", type=float, default=12.0)
    ap.add_argument("--join-wait", type=float, default=24.0)
    ap.add_argument("--join-attempts", type=int, default=3)
    ap.add_argument("--settle", type=float, default=0.0, help="extra wait after the offer is seen")
    ap.add_argument("--offer-delta", type=float, default=25.0, help="luma change that means the offer is up")
    ap.add_argument("--offer-interval", type=float, default=0.25)
    ap.add_argument("--offer-timeout", type=float, default=40.0)
    ap.add_argument("--vs-timeout", type=float, default=25.0)
    ap.add_argument("--step-wait", type=float, default=4.0)
    ap.add_argument("--cut-off", action="store_true", help="P1 taps CUT OFF")
    ap.add_argument("--vs", action="store_true", help="P1 taps VS on the game-select screen")
    ap.add_argument("--press-a", action="store_true")
    ap.add_argument("--watch", type=float, default=0.0, help="keep reporting for N seconds afterwards")
    ap.add_argument("--show", default="", help="comma-separated step names to print as ASCII")
    args = ap.parse_args()
    args.show = {s for s in args.show.split(",") if s}

    lines = []
    runner = Runner(args, lines)
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
