#!/usr/bin/env python3
"""Drive a title's N cart consoles into one room and close the lobby, gated on radio.

Why this file exists (and why it is here rather than in /tmp): the route from
boot to MKDS's room was learned three times over and lost twice, because it lived
in a scratch driver that each pass deleted. This is that route, durable and
measured, in the repo, for a title and a console count.

Four things it encodes, measured on 2026-10-15/16 and recorded in
compatibility.md's `2026-10-16f`:

  * **The room is named by the frame the game hands the bus, not by `role` - but
    a room-sized frame is not a room.** MKDS hands a 72-byte `fc=0080` frame and
    the bus mask takes its own bit *while `role` still reads 0*; `role` flips to
    1 only pages later. That much was measured on 2026-10-15/16 and still holds.
    What the *next* run (2026-10-16, mkds4p3) corrected is the claim built on
    it: the frame is armed on the page after MULTIPLAYER by **every** console,
    not only by a host, so "my bit plus a >=64-byte frame" fires on the section
    page for all four consoles at once (`p1..p4` all `handed=0080/72b`, all on
    the one page `193382FD`). The gate is therefore `in_section`, and the room is
    what `create_room()`'s press produces from there - a change the *other*
    consoles' lists show too.
  * **A press is not proven by "did the picture change".** MKDS's screens
    animate, so every press changes the digest; a search that trusts it circles
    between two states of one dialog (measured: 78485022 <-> 790742fa with the
    radio idle for seven presses). The host's walk below is monotone over
    (page, input) pairs - no input is pressed twice on the same page - so it
    cannot loop, and every step is logged with the console's own radio state.
  * **A joiner is not a host with different inputs, and not a fixed sequence
    either.** The earlier version handed a joiner the host's own enumerated action
    set, most likely first, so a joiner reached CREATE GROUP on the group list and
    hosted its own room (`role=1`, its own 72/116-byte frames) while the bus mask
    still read 0xf. The version after that gave it the title's two taps in order
    (TITLE, MULTIPLAYER) on the assumption it was on the title screen - so on a
    console that had already passed the title (MKDS's attract loop advances by
    itself) the MULTIPLAYER tap landed on whatever page was up and the walk never
    reached the list. `join_route()` now advances on whatever page the console is
    actually showing (`JOIN_ADVANCE`, one input per distinct page, gated on the
    radio arming in `in_section`) and then waits for the room to be *listed on
    that console's own screen* (`group_listed`). Both are its own state: the page
    it is drawing and the frames its own game is handing the bus.
  * **The joins go back to back, all of them last.** A linked MKDS client leaves
    the lobby by itself (measured at anywhere from ~2,740 emulated frames after
    joining to more than 10,500), so `join_route()` walks every joiner to its
    group list and returns, then every bar is tapped (`tap_group_bar`) before any
    of them is proved (`await_room`). Joining one at a time reads `0x3 -> 0x1 ->
    0x5` and never `0x7` at four consoles (compatibility.md 2026-09-29;
    reproduced live on 2026-10-16, once by the walk order and once by proving
    each join before tapping the next).

  * **The coupled room can be driven from each console's own screen**
    (`--press-room`). The route's default press arm lands on coordinates it
    already knows (CUT OFF, VS, A), which cannot answer "the title wanted an input
    this route never sent": that arm presses the host only, and the guests' own
    walk to 48/64 and deauth 130-200 emulated frames after coupling (compatibility
    .md 2026-10-16w) happens with nothing pressed on them. The other arm reads
    each console's own screen, finds the control *that* console is drawing (a
    text band in the lower half of its touch screen) and presses its centre, so
    what a console gets is a reading of its own page rather than a guess. A
    console whose screen draws no such band is reported as offering no control
    and is not pressed at all.

Every wait is the app's own emulated frames (`status`'s per-console `seq`), never
a wall clock: at four consoles this title runs at ~44% of DS speed, which is what
made the older wall-clock routes land mid-transition. Pages are waited for, not
slept on: `wait_stable()` holds on the console's own framebuffer
(`probe`'s per-screen CRC, a native read rather than a PNG per poll), because a
press read against a fixed frame count lands mid-transition and the "after" page
is then a page the input never produced.

    tools/scripts/mkds-4p.py                          # 4 consoles, MKDS, fresh app
    tools/scripts/mkds-4p.py --players 3 --attach      # add consoles to a live session
    tools/scripts/mkds-4p.py --rom "... .nds" --tag myrun

stdout is the timeline; it is also written to logs/<tag>_route.log and the app's
own log is logs/<tag>_app.log for the link evidence behind every gate. Exit 0
when the host's room exists and every requested joiner is on the bus, 1 when a
console stops on a page that exhausts every input (the page is then named from
the game's own text), 2 when the app never came up.
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from appctl import Channel  # noqa: E402

from PIL import Image  # noqa: E402

DEFAULT_ROM = os.path.join(ROOT, "Test Roms/0168 - Mario Kart DS (U)(SCZ).nds")
# The joiner's route, in the title's own order (see the module docstring).
TITLE_TAP = (128, 96)     # the title screen answers only here
MULTI_TAP = (128, 68)     # MULTIPLAYER on the main menu
# On the page after MULTIPLAYER (the group list), the control that creates the
# group is the one along the bottom of the page, inside the band `bands` reports
# at y 152..164. Measured 2026-10-16 (mkds4p4): the documented-looking tap at
# (128,96) sits in the *gap between two of the page's four group slots* - it
# moved no console's page at all (p1, p2, p3 all held 193382FD across it), which
# is what the older note "TITLE (128,96) on the page after MULTIPLAYER creates
# the room" had attributed to creating a room.
CREATE_TAP = (128, 157)
GROUP_BAR = (127, 23)     # the host's group, drawn along the top of the group list
DIALOG_YES = (67, 145)    # the confirm button on the game's own dialogs
CONFIRM_PRESS = ("press", 1)  # A: the input the create screen waits on before the radio returns
CUT_OFF_TAP = (149, 167)  # the control that only exists once a second racer is in
VS_TAP = (128, 22)        # VS on the game-select screen
# What a *joiner* may press to advance its own game, and nothing else. CREATE and
# the mode-select pair are deliberately absent: a joiner handed the host's action
# set presses CREATE on the group list and hosts its own room (measured
# 2026-10-15/16: two joiners at `role=1` with their own room frames while the bus
# mask read 0xf). Each is tried on the page the console is actually showing, in
# this order, once per page (see `join_route`).
JOIN_ADVANCE = [
    ("TITLE", ("tap", TITLE_TAP)),
    ("MULTI", ("tap", MULTI_TAP)),
    ("DIALOG", ("tap", DIALOG_YES)),
    ("A", ("press", 1)),
]
# The group list's own rows, in a joiner's own frames (bottom screen): the four
# slots the list draws, top first. The room is listed in the *first* one, and the
# reading that says so is that row against the empty slots beside it - an
# absolute reading, not a page change, because a group created while this console
# was still walking up to the list produces no change on it at all (measured
# 2026-10-16: three joiners sat on the list, page identical, because the host's
# radio had gone down at its create press and nothing was ever listed).
LIST_ROWS = [(8, 16, 240, 16), (8, 47, 240, 16), (8, 78, 240, 16), (8, 109, 240, 16)]
ROW_MARGIN = 20.0
# The patch of the host's touch screen that CUT OFF appears in, empty blue until a
# second racer has joined (tools/mkds_join.py measured the same rectangle).
CUT_OFF_RECT = (84, 151, 132, 41)
# The documented inputs, most likely first (compatibility.md's `2026-10-16f`:
# TITLE on the page after MULTIPLAYER is what creates the room). The host's walk
# searches these; a joiner does not (see `join_route`).
ACTIONS = [
    ("TITLE", ("tap", (128, 96))),
    ("MULTI", ("tap", (128, 68))),
    ("CREATE", ("tap", (128, 157))),
    ("NORMAL", ("tap", (128, 68))),
    ("JOINBAR", ("tap", (127, 23))),
    ("DIALOGYES", ("tap", (67, 145))),
    ("A", ("press", 1)),
    ("START", ("press", 8)),
    ("DOWN", ("press", 128)),
    ("B", ("press", 2)),
]
def parse_glyphs(dump):
    """tools/glyphs.py's dump, as blocks of (x0, x1, width, lines).

    The width is what tells a letter from a border (see `read_page`).
    """
    blocks = []
    for ln in dump.splitlines():
        m = re.match(r"--- glyph \d+ \(x=(\d+)\.\.(\d+), (\d+)px\)", ln)
        if m:
            blocks.append({"x0": int(m.group(1)), "x1": int(m.group(2)),
                           "width": int(m.group(3)), "lines": []})
        elif blocks:
            blocks[-1]["lines"].append(ln)
    return blocks


class Runner:
    def __init__(self, port, app_log, tag, lines):
        self.ch = Channel(port, timeout=60)
        self.ch.call(cmd="ping")
        self.app_log = app_log
        self.tag = tag
        self.lines = lines

    def say(self, text):
        print(text, flush=True)
        self.lines.append(text)

    # ---- the frame clock -------------------------------------------------
    def frames(self, players):
        return {f["player"]: f["seq"] for f in self.ch.call(cmd="status")["frames"]}

    def mask(self):
        return self.ch.call(cmd="status")["link_mask"]

    def wait_frames(self, n, players):
        base = self.frames(players)
        while True:
            cur = self.frames(players)
            if all(cur[p] - base[p] >= n for p in players):
                return
            time.sleep(0.04)

    def page(self, p):
        """This console's own picture: (top CRC, bottom CRC) from the renderer.

        A native read, not a PNG: the walk polls for settled pages, and a shot
        per poll at four consoles is what made the old budget too coarse to use.
        """
        fb = self.ch.call(cmd="probe", player=p)["framebuffer"]
        return (fb["top"]["crc"], fb["bottom"]["crc"])

    def wait_stable(self, p, hold=30, timeout=600):
        """Wait until this console's own picture has held for `hold` frames.

        MKDS animates its title screen and its menu transitions, so a press
        whose result is read after a fixed frame count is read mid-transition:
        the "after" page is then a page the input never produced, and a walk
        gated on it spends its budget on presses that did nothing. Holding the
        console's own framebuffer is the settle that fixes that.
        """
        start = self.frames((p,))[p]
        last = None
        while True:
            now = self.frames((p,))[p]
            cur = self.page(p)
            if last and cur == last[1] and now - last[0] >= hold:
                return cur
            if now - start > timeout:
                return cur
            last = (now, cur)
            time.sleep(0.05)

    def wait_join(self, p, frames=600):
        """Wait for this console to be a client on the bus (see `joined`)."""
        start = self.frames((p,))[p]
        while self.frames((p,))[p] - start < frames:
            if self.joined(p):
                return True
            time.sleep(0.1)
        return self.joined(p)

    def luma(self, p, rect):
        """Mean luma (0-255) of a patch of this console's touch screen.

        The screen half of the join evidence: the CUT OFF control exists only
        while a second racer is in the room, so this patch is empty blue for one
        racer and carries a button for two. Read through the frontend's own
        frames (same expression tools/mkds_join.py uses).
        """
        x, y, w, h = rect
        js = (
            "(async () => { const b = await window.__mdctl.frames(); if (!b) return -1;"
            " const p = window.__mdctl.unpack(b);"
            f" const o = p.at({p - 1}) + window.__mdctl.BPS, px = p.u8;"
            " let s = 0, n = 0;"
            f" for (let yy = {y}; yy < {y + h}; yy++)"
            f"  for (let xx = {x}; xx < {x + w}; xx++) {{"
            "   const i = o + (yy * 256 + xx) * 4;"
            "   s += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]; n++; }"
            " return n ? s / n : -1; })()"
        )
        return float(self.ch.call(cmd="eval", expr=js))

    # ---- the oracle ------------------------------------------------------
    def digests(self, players):
        """One composite shot carries every console's own screens."""
        out = self.ch.call(cmd="shot", name=f"{self.tag}_poll")
        im = Image.open(out["path"]).convert("RGB")
        return {p: hashlib.md5(im.crop(((p - 1) * 256, 0, (p - 1) * 256 + 256, im.height)).tobytes()).hexdigest()[:8]
                for p in players}

    def wifi(self, p):
        return str(self.ch.call(cmd="wifiio", player=p))

    def handed(self, p):
        """(fc, length) of the frame this console's own game handed the bus."""
        m = re.search(r"handed: start=([0-9A-Fa-f]+) len=(\d+) fc=([0-9A-Fa-f]+)", self.wifi(p))
        return (m.group(3), int(m.group(2))) if m else ("", 0)

    def beacons(self, p):
        m = re.search(r"bcn ([0-9A-Fa-f]+) /", self.wifi(p))
        return int(m.group(1), 16) if m else 0

    def role(self, p):
        m = re.search(r"role=(\d+)", self.wifi(p))
        return int(m.group(1)) if m else -1

    def state(self, p):
        fc, ln = self.handed(p)
        return f"role={self.role(p)} handed={fc}/{ln}b bcn={self.beacons(p)} mask={self.mask():#x}"

    def armed(self, p):
        """Has this console's game entered a linked section at all?

        Its own bit on the bus, or frames of its own in its hand: MKDS arms its
        radio when it enters the multiplayer section, so this is "this console is
        in the section" as opposed to "this console is on a title screen". A
        joiner that has already walked this far must not be walked again from the
        title - the same two presses then land on whatever the deeper page draws
        at those coordinates.
        """
        _fc, ln = self.handed(p)
        return bool(self.mask() & (1 << (p - 1))) or ln > 0

    def in_section(self, p):
        """This console is on the multiplayer section page: its own bit on the
        bus and a room-sized frame in its hand. `role` is deliberately not part
        of the gate - it lags the frame (see the module docstring).

        This is *not* "the room exists". Measured 2026-10-16 (mkds4p3): every
        console that reaches the page after MULTIPLAYER hands the 72-byte
        fc=0080 frame with role=0, so a gate made of these two facts fires on the
        section page for all four consoles at once. The earlier version called
        that gate `hosting` and stopped the host's walk on it, so no group was
        ever created and three joiners sat on the host's own page digest tapping
        a group bar with no group under it. The room is what `create_room()`
        produces from here.
        """
        _fc, ln = self.handed(p)
        return bool(self.mask() & (1 << (p - 1))) and ln >= 64

    def create_room(self, p, others=()):
        """Create the group on the section page, and watch what the others see.

        The creating press is the group list's own bottom control (`CREATE_TAP`),
        and it is **not the whole create**. Measured on 2026-10-16 in two
        instrumented runs and then confirmed live: at that press the console's
        game powers its own radio down (PowerState/PowerTX/PowerUS down, `W_IE`
        cleared, the US counter stops, the ARM9 keeps running, the picture holds
        on the new page) and the bus sees `wireless off (end #2)`. That state is
        not a hang and not a dropped session - the game waits on its own screen
        for one more input, and the **confirm press (A)** is what brings the
        radio back up as host (`wireless ON (begin #3)`, `role=1`, a fresh 72-byte
        frame further into RAM). So the gate that says the room is live is the
        radio coming back, and a radio that stays down is a stop worth reporting.
        """
        before = self.page(p)
        others_before = {o: self.page(o) for o in others}
        self.act(p, ("tap", CREATE_TAP))
        mid = self.wait_stable(p)
        self.say(f"host-create tap {CREATE_TAP}: p{p} {before[1][:8]} -> {mid[1][:8]} | {self.state(p)}"
                 " (the game's own transition: its radio is down until the confirm)")
        self.act(p, CONFIRM_PRESS)
        start = self.frames((p,))[p]
        while self.frames((p,))[p] - start < 600:
            if (self.mask() & (1 << (p - 1))) and self.handed(p)[1] >= 64:
                break
            time.sleep(0.1)
        after = self.wait_stable(p)
        self.say(f"host-confirm A: p{p} {mid[1][:8]} -> {after[1][:8]} | {self.state(p)} - "
                 + ("the radio is back up with the room on it"
                    if self.mask() & (1 << (p - 1)) else
                    "the radio stayed down after the confirm (this is the stop to report)"))
        for o in others:
            now = self.page(o)
            self.say(f"  p{o} {'saw the group appear' if now != others_before[o] else 'saw no change'}"
                     f" ({others_before[o][1][:8]} -> {now[1][:8]}) | {self.state(o)}")
        return after

    def joined(self, p):
        """A client on the bus: its own bit, frames of its own, and the guest role.

        The bus bit and the handed frames are the console's own account of
        itself; `role` is the third, independent one and is what separates a
        console that joined from a console that only self-hosted (measured
        2026-10-16: a self-hosting joiner has its bit and its own 72/116-byte
        frames too). A joiner that leaves the guest role behind says so here.
        """
        _fc, ln = self.handed(p)
        return bool(self.mask() & (1 << (p - 1))) and ln > 0 and self.role(p) == 2

    def act(self, p, spec):
        kind, arg = spec
        if kind == "tap":
            self.ch.call(cmd="touch", player=p, x=arg[0], y=arg[1], ms=150)
            self.ch.call(cmd="untouch", player=p)
        else:
            self.ch.call(cmd="press", player=p, buttons=arg, ms=180)

    # ---- naming a page from the game's own text ---------------------------
    # Where each screen sits inside the composite shot (512x384 for two consoles:
    # player N's column is 256 wide at (N-1)*256, the top screen on top of the
    # bottom one). The band coordinates `bands` reports are screen-local.
    SCREEN_Y = {"top": 0, "bottom": 192}

    def read_page(self, p, label):
        """Name the page this console is on from the game's own text.

        Bands are ranked by *glyph count* (tools/glyphs.py's own header) rather
        than by the band scorer: on a menu page the scorer's top band is the
        solid title bar, whose dump is a picture and names nothing - which is
        how the 2026-10-16 read picked a blank band and named no page at all.
        The band that is read, and its crop, are printed with the glyphs, so a
        band that will not decode is evidence rather than a guess.
        """
        # The shot keeps the read's own label: the page a stop happened on is
        # evidence, and a fixed name means the next read of the same console
        # overwrites it before anyone can look (found on 2026-10-16, reading the
        # host's own room page back out of a shot the next read had replaced).
        slug = "".join(c if c.isalnum() else "_" for c in label)[:40]
        out = self.ch.call(cmd="shot", name=f"{self.tag}_{slug}_p{p}")
        png, left = out["path"], (p - 1) * 256
        for screen in ("bottom", "top"):
            bands = subprocess.run(
                ["python3", os.path.join(ROOT, "tools/appctl.py"), "bands", "--player", str(p),
                 "--screen", screen], cwd=ROOT, capture_output=True, text=True).stdout
            rows = [ln for ln in bands.splitlines() if re.match(r"y \d+", ln)]
            best = None
            for row in rows[:5]:
                y = int(re.findall(r"y (\d+)", row)[0])
                h = int(re.findall(r"h (\d+)", row)[0]) + 4
                for mode in ("dark", "light"):
                    out_g = subprocess.run(
                        ["python3", os.path.join(ROOT, "tools/glyphs.py"), png, str(left),
                         str(self.SCREEN_Y[screen] + y), "256", str(max(h, 10)), "120", mode],
                        cwd=ROOT, capture_output=True, text=True).stdout
                    blocks = parse_glyphs(out_g)
                    # A run wider than a character is a border or a bar, not a
                    # letter: the group list's rows are outlined bars, and the
                    # first version of this read counted their borders as text
                    # and so named the page from a box.
                    letters = [b for b in blocks if b["width"] <= 12]
                    cand = (len(letters), y, h, mode, letters, len(blocks))
                    if best is None or cand[0] > best[0]:
                        best = cand
            if best and best[0] >= 3:
                count, y, h, mode, letters, total = best
                shown = "\n".join("    ------------------ glyph "
                                  f"(x={b['x0']}..{b['x1']}, {b['width']}px)\n"
                                  + "\n".join("    " + ln for ln in b["lines"])
                                  for b in letters[:4])
                self.say(f"  p{p} {screen} page ({label}): band y={y} h={h} {mode} ink, "
                         f"{count} letter-width glyph(s) of {total} runs "
                         f"(crop {left},{self.SCREEN_Y[screen] + y} 256x{max(h, 10)})\n{shown}")
                return
        self.say(f"  p{p} page ({label}): no band on either screen holds 3+ letter-width glyphs")

    # ---- the interaction arm: press only what a console's own screen draws ----
    # A touch control is text inside a button in the lower half of the touch
    # screen: MKDS draws its group-list bar at y 152..164 and CUT OFF's label
    # inside the patch at y 151..192. The group list's own rows (y 16..125) are
    # outlined bars, above this line and too wide to be letters, so "lower half"
    # plus "runs no wider than a word" is what separates a control from the list
    # and the box it sits in: a border or a slot's bar spans the screen (the
    # group list's rows measure 240px), while a label - letters with 1px gaps,
    # which is why a word reads as one run and not one run per letter - stays
    # under `CONTROL_MAX_RUN`. The height floor is the DS font's own: a band
    # shorter than a line of text is a pattern, not a word - the joiner's own room
    # page draws a striped panel whose stripes otherwise read as letters
    # (measured 2026-10-16, `mkds2p_presssmoke1`: the only band it offered in the
    # lower half was 5 rows tall, and my first reader pressed two of its stripes).
    CONTROL_MIN_Y = 96
    CONTROL_MIN_H = 7
    CONTROL_MAX_RUN = 64
    CONTROL_GAP = 8
    CONTROL_BANDS = 3
    CONTROL_INK = 120

    def band_runs(self, im, left, band):
        """Count one band's letter runs here, in this process.

        `bands` gives the row a text line is drawn on, but its bounds take in the
        box that line sits in (the host's own room page: band y 163..177 while the
        letters are on y 162..174), and a border row inks every column, so a crop
        on the band's own bounds merges into one run and reads as "no letters".
        Trimming the band is what tells a word from the box around it, and doing
        that here rather than one subprocess per crop keeps a read inside the
        window this arm has to fit in (see `press_room`).

        Returns the best reading: the runs that are no wider than a word, how many
        other runs the crop held, whether it leaves blank rows (a line of text
        does, a filled or striped band does not) and the ink polarity that read
        it.
        """
        h, y = band["h"], self.SCREEN_Y["bottom"] + band["y"]
        px = im.crop((left, y, left + 256, y + h)).load()
        best = None
        for inset in (1, 3, 0):
            rows = range(inset, h - inset)
            if len(rows) < self.CONTROL_MIN_H:
                continue
            for light in (False, True):
                def ink(x, r):
                    return px[x, r] > self.CONTROL_INK if light else px[x, r] < self.CONTROL_INK
                cols = [any(ink(x, r) for r in rows) for x in range(256)]
                runs, start = [], None
                for x, v in enumerate(cols + [False]):
                    if v and start is None:
                        start = x
                    elif not v and start is not None:
                        runs.append([start, x - 1])
                        start = None
                merged = []
                for r in runs:
                    # A 1-2px gap is inside a glyph, not between two
                    # (tools/glyphs.py splits them the same way).
                    if merged and r[0] - merged[-1][1] <= 2:
                        merged[-1][1] = r[1]
                    else:
                        merged.append(r)
                words = [r for r in merged if 3 <= r[1] - r[0] + 1 <= self.CONTROL_MAX_RUN]
                blank = any(not any(ink(x, r) for x in range(256)) for r in rows)
                cand = (len(words), blank, inset, light, words, len(merged))
                if best is None or cand[:2] > best[:2]:
                    best = cand
        if best is None:
            return None
        return {"words": best[4], "inset": best[2], "light": best[3], "runs": best[5],
                "blank": best[1]}

    def screen_bands(self, p, label):
        """This console's own bottom screen, read as its text bands.

        One pass of the reader `read_page` uses, but the whole band list comes
        back instead of the best band: this arm's claim is that a console is
        pressed only where its own screen draws a control, so the bands that were
        *not* picked are part of the evidence. Only the bottom-most
        `CONTROL_BANDS` candidates are measured (see `band_runs`).
        """
        out = self.ch.call(cmd="shot", name=f"{self.tag}_{label}_p{p}")
        png, left = out["path"], (p - 1) * 256
        bands = subprocess.run(
            ["python3", os.path.join(ROOT, "tools/appctl.py"), "bands", "--player", str(p),
             "--screen", "bottom"], cwd=ROOT, capture_output=True, text=True).stdout
        rows = []
        for ln in bands.splitlines():
            m = re.match(r"y (\d+)\.\.(\d+)\s+h (\d+)\s+score (\d+)", ln)
            if m:
                rows.append({"y": int(m.group(1)), "y1": int(m.group(2)), "h": int(m.group(3)),
                             "score": int(m.group(4)), "read": None})
        im = Image.open(png).convert("L")
        for b in [r for r in rows
                  if r["y"] >= self.CONTROL_MIN_Y and r["h"] >= self.CONTROL_MIN_H
                  ][-self.CONTROL_BANDS:]:
            b["read"] = self.band_runs(im, left, b)
        return png, left, rows

    def control_band(self, p, label):
        """The control this console's own screen is offering, or None.

        The control is the bottom-most band in the lower half of the touch screen
        that measures as a word (see `band_runs`): a *reading* of that screen, not
        a coordinate this route already knew. `None` is the answer for a screen
        that draws no such band, and it is printed as a reading of its own - the
        band list goes into the log either way, so a screen that never offered a
        control is distinguishable from a control that was pressed and did
        nothing.
        """
        png, left, rows = self.screen_bands(p, label)

        def desc(b):
            r = b["read"]
            if r is None:
                # Not measured is *not* "measured and rejected": a band above the
                # lower half or shorter than a line of text was never read (the
                # report has to tell a screen that drew nothing pressable from one
                # whose band read as a pattern).
                why = ("above the lower half" if b["y"] < self.CONTROL_MIN_Y
                       else f"h{b['h']} under a text line" if b["h"] < self.CONTROL_MIN_H
                       else "no runs at any trim")
                return f"y{b['y']}..{b['y1']}(h{b['h']},s{b['score']},not measured: {why})"
            return (f"y{b['y']}..{b['y1']}(h{b['h']},s{b['score']},"
                    f"{'bright' if r['light'] else 'dark'} {len(r['words'])}word(s)/"
                    f"{r['runs']}run(s),inset{r['inset']}{'' if r['blank'] else ',no blank rows'})")
        self.say(f"  p{p} its own screen ({label}): bands "
                 + ("  ".join(desc(b) for b in rows) or "none"))
        for b in reversed(rows[-self.CONTROL_BANDS:]):
            r = b["read"]
            if r is None or len(r["words"]) < 2:
                continue
            # The report is the runs themselves, read off the crop the reading
            # above chose (tools/glyphs.py's own header: this is how a block is
            # matched against a known screen, and how a stripe is told from a
            # word). The dump's x is absolute in the shot while the runs measured
            # by `band_runs` are on this console's own 256 columns - which is what
            # a touch wants - so the report prints the crop it read and the two
            # are told apart there rather than by arithmetic.
            inset, gh = r["inset"], b["h"] - 2 * r["inset"]
            dump = subprocess.run(
                ["python3", os.path.join(ROOT, "tools/glyphs.py"), png, str(left),
                 str(self.SCREEN_Y["bottom"] + b["y"] + inset), "256", str(gh),
                 str(self.CONTROL_INK), "light" if r["light"] else "dark"],
                cwd=ROOT, capture_output=True, text=True).stdout
            blocks = [g for g in parse_glyphs(dump) if g["width"] <= self.CONTROL_MAX_RUN]
            # A label is a cluster of runs joined by word gaps (a space is wider
            # than the 1-2px inside a glyph but narrower than the gap between a
            # page's separate elements), and the widest cluster in the band is
            # the control: on the host's own room page that is the button's own
            # two words at x 114..182, against the page's emblems at x 10..30 and
            # x 213..255 which share its rows.
            clusters = []
            for run in r["words"]:
                if clusters and run[0] - clusters[-1][1] <= self.CONTROL_GAP:
                    clusters[-1][1] = run[1]
                    clusters[-1][2] += 1
                else:
                    clusters.append([run[0], run[1], 1])
            best = max(clusters, key=lambda c: (c[1] - c[0], c[2]))
            x0, x1 = best[0], best[1]
            shown = "\n".join(
                f"    ---------- glyph (x={g['x0']}..{g['x1']}, {g['width']}px)\n"
                + "\n".join("    " + ln for ln in g["lines"]) for g in blocks[:8])
            self.say(f"  p{p} control band y={b['y']}..{b['y1']} h={b['h']}, "
                     f"{'bright' if r['light'] else 'dark'} ink, {len(r['words'])} word-width "
                     f"run(s) of {r['runs']} on rows {b['y'] + inset}..{b['y1'] - inset} "
                     f"(crop {left},{self.SCREEN_Y['bottom'] + b['y'] + inset} 256x{gh}), "
                     f"label cluster x {x0}..{x1} of its own 256 "
                     f"({best[2]} run(s), {len(clusters)} cluster(s))\n{shown}")
            return {"y": b["y"], "h": b["h"], "x0": x0, "x1": x1, "x": (x0 + x1) // 2,
                    "png": png}
        self.say(f"  p{p} its own screen draws no control band below y={self.CONTROL_MIN_Y} "
                 f"that reads as a word: nothing is pressed on it")
        return None

    def press_console_control(self, p):
        """Press this console's own control, at the centre of what it drew.

        The coordinate is this console's own picture's, not one this route already
        knew. The screen is read on both sides of the press - the band and its
        runs before, the bands again after the page settles (`read_after_press`) -
        so a screen that drew no control is distinguishable from a control that did
        nothing, and neither is inferred from "the picture changed" (MKDS's pages
        animate, see the module docstring).

        The reading after the press is deliberately *not* taken here: waiting for
        this console's page to settle between two consoles' presses would push the
        later presses out of the window being tested, which is what `press_room`
        exists to land inside. Returns (the page before the press, the control or
        None).
        """
        before = self.page(p)
        ctl = self.control_band(p, "before_press")
        if ctl is None:
            return before, None
        x, y = ctl["x"], ctl["y"] + ctl["h"] // 2
        self.ch.call(cmd="touch", player=p, x=x, y=y, ms=150)
        self.ch.call(cmd="untouch", player=p)
        self.say(f"  p{p} pressed its own screen's control at ({x},{y}) of its own 256x192")
        return before, ctl

    def read_after_press(self, p, before, had_control):
        """What this console's own screen shows once the press has settled.

        The hold and the ceiling are short on purpose: this read happens after
        every console has been pressed, so a console whose page never settles must
        not hold up the next console's read. The bus mask and this console's own
        radio state are read *before* that wait, not after it: the room can end
        inside it (the settle's ceiling is 120 emulated frames), and a state read
        on the far side of that would date the console's own leave no better than
        "sometime during the settle" - measured 2026-10-16 (`mkds4p_pressroom`: the
        first after-read already saw mask 0x0, so the whole teardown hid inside one
        settle).
        """
        opened = self.mask()
        opened_state = self.state(p)
        after = self.wait_stable(p, hold=10, timeout=120)
        self.say(f"  p{p} as its press settles (mask={opened:#x} was the bus when this read "
                 f"began): {opened_state}")
        if had_control:
            self.control_band(p, "after_press")
            self.say(f"  p{p} press: {before[1][:8]} --its own control--> {after[1][:8]} "
                     f"| {self.state(p)}")
        else:
            self.say(f"  p{p} press: {before[1][:8]} -> {after[1][:8]} with nothing pressed on "
                     f"it (its own screen drew no control) | {self.state(p)}")

    # ---- the monotone walk ----------------------------------------------
    def walk(self, p, want, budget=12, actions=None):
        """Monotone in (page, input): returns (reached, path, stuck_page).

        Each step waits for the target's page to settle before it is read, and
        the step is logged with the console's own radio state, so a press that
        landed during a transition cannot be mistaken for a page that ignored
        it.
        """
        actions = actions or ACTIONS
        used, path = {}, []
        for _ in range(budget):
            if want(p):
                return True, path, None
            here = self.page(p)
            used.setdefault(here, set())
            progressed = False
            for name, spec in actions:
                if name in used[here]:
                    continue
                used[here].add(name)
                before = self.page(p)
                self.act(p, spec)
                after = self.wait_stable(p)
                self.say(f"p{p} {before[1][:8]} --{name}--> {after[1][:8]} | {self.state(p)}")
                if want(p):
                    path.append((name, before, after))
                    return True, path, None
                if after != before and after not in used:
                    path.append((name, before, after))
                    progressed = True
                    break
            if not progressed:
                return False, path, here
        return want(p), path, None

    def list_rows(self, p):
        """The group list's own rows, read out of this console's own frames."""
        return [self.luma(p, rect) for rect in LIST_ROWS]

    def group_listed(self, p):
        """Is a room listed on *this* console's own group list?

        The list draws four slots; the room takes the first, so the reading is
        that row against the empty ones beside it in the same sample (not a
        threshold nobody calibrated, and not "the page changed" - a group created
        before this console walked up to the list never changes it).
        """
        vals = self.list_rows(p)
        empties = sorted(vals[1:])
        med = empties[len(empties) // 2]
        return (vals[0] - med) >= ROW_MARGIN

    def join_route(self, p, group_wait=900):
        """Walk one joiner to the host's group list, from wherever its own page is.

        Returns (listed, stopped_page, path). Both phases are gated on this
        console's own state rather than on a fixed coordinate sequence - which is
        what the previous version was, and why it failed: it tapped TITLE then
        MULTIPLAYER on the assumption the console was on the title screen, so on
        a console that had already passed the title (MKDS's attract loop advances
        on its own) the MULTIPLAYER tap landed on whatever page was up and the
        walk never reached the list at all.

        Phase 1 advances on whatever page this console is showing: each input in
        `JOIN_ADVANCE` is tried once per distinct page, and the phase ends when
        the radio arms (`in_section`: its own bus bit and a room-sized frame in
        its hand), which is the console's own account of "I am in the section".
        No input in the set creates a group, so this cannot make the console a
        second host.

        Phase 2 is the room's listing, read from this console's frames: the list
        is not joinable until a room is drawn on it, so this waits on
        `group_listed` and a list that never lists one is reported as the stop.

        This deliberately does *not* tap the group bar - `tap_group_bar()` does,
        and it is called for every joiner only once they are all on their lists. A
        linked MKDS client leaves the lobby on its own (measured anywhere from
        ~2,740 emulated frames after the join to more than 10,500), so a joiner
        that joins while the next one is still being walked is gone before that
        one arrives: at four consoles that reads as `0x3 -> 0x1 -> 0x5`, never
        `0x7` (compatibility.md 2026-09-29, and today's run reproduced it).
        """
        path = []
        used = {}
        for _ in range(len(JOIN_ADVANCE) * 4):
            if self.joined(p):
                return True, None, path
            if self.in_section(p):
                break
            here = self.page(p)
            used.setdefault(here, set())
            moved = False
            for name, spec in JOIN_ADVANCE:
                if name in used[here]:
                    continue
                used[here].add(name)
                before = self.page(p)
                self.act(p, spec)
                after = self.wait_stable(p)
                self.say(f"p{p} join-{name} {spec[1]}: {before[1][:8]} -> {after[1][:8]} | {self.state(p)}"
                         + (" - in the section" if self.in_section(p) else ""))
                path.append((name, before, after))
                if self.in_section(p):
                    moved = True
                    break
                if after != before and after not in used:
                    moved = True
                    break
            if not moved:
                self.say(f"p{p} stopped advancing on page {here[1][:8]}: every join input tried on it, radio idle")
                return False, here, path
        else:
            self.say(f"p{p} never reached the link section")
            return False, self.page(p), path

        # Phase 2: the listing, from this console's own frames.
        rows = self.list_rows(p)
        self.say(f"p{p} is on the section page | {self.state(p)} | list rows "
                 + " ".join(f"{v:.0f}" for v in rows)
                 + f" | group listed: {self.group_listed(p)}")
        start = self.frames((p,))[p]
        while self.frames((p,))[p] - start < group_wait:
            if self.joined(p):
                return True, None, path
            if self.group_listed(p):
                rows = self.list_rows(p)
                self.say(f"p{p} its own list now shows a group: rows " + " ".join(f"{v:.0f}" for v in rows))
                break
            time.sleep(0.2)
        else:
            rows = self.list_rows(p)
            self.say(f"p{p} the list never listed a group: rows " + " ".join(f"{v:.0f}" for v in rows)
                     + " (every slot reads the same: nothing reached this console)")
            self.read_page(p, f"the empty group list p{p} waited on")
            return False, self.page(p), path

        self.read_page(p, "the group list a joiner is standing on")
        return True, None, path

    def tap_group_bar(self, p):
        """Press the room's own row on this console's list.

        Separated from the proof on purpose: the joins have to *overlap*, and a
        press followed by a settle-wait and a join-wait per console spends that
        window one joiner at a time. Measured live on 2026-10-16: with the prove
        wait in the loop, p2 and p3 were out of the room again by the time p4's
        bar was tapped (`mask 0x1`, the host's own CUT OFF patch unchanged), even
        though all three bars are on the same page and the taps themselves take
        milliseconds.
        """
        before = self.page(p)
        self.act(p, ("tap", GROUP_BAR))
        self.say(f"p{p} join-GROUPBAR tap {GROUP_BAR}: list page {before[1][:8]} | {self.state(p)}")

    def await_room(self, p, tries=2, frames=300):
        """Prove the join from this console's own state; retry the bar if it did not take.

        The proof is this console's own account of itself: its bus bit, its own
        handed frames and the guest role (`joined`). A console the game has moved
        off the list is reported rather than tapped blind at the same
        coordinates.
        """
        ok = self.wait_join(p, frames=frames)
        self.say(f"p{p} room taken: {ok} | {self.state(p)} | page {self.page(p)[1][:8]}")
        for attempt in range(tries):
            if ok:
                return True
            if not self.in_section(p):
                self.say(f"p{p} is not on its list any more ({self.state(p)})")
                break
            self.tap_group_bar(p)
            ok = self.wait_join(p, frames=frames)
            self.say(f"p{p} room taken (retry {attempt + 1}/{tries}): {ok} | {self.state(p)}")
        return ok

    # ---- the leave instrument --------------------------------------------
    #
    # What the room's own leave can only be asked while the session is running,
    # and what the 2026-10-16 hold run's log could not answer from outside: which
    # code the console that leaves runs across the window, which values that code
    # ran on, and what it had read out of its own memory first. `pctrace` is the
    # fork's PC recorder (every entry a real PC - under the shipped JIT one basic
    # block - with lr/cpsr/r0-r3 beside it); `mem` reads that console's own RAM, so
    # the code the report names can be read back and disassembled.
    ARM9_LO, ARM9_HI = 0x02000000, 0x02400000   # the ARM9's main RAM, where a
                                                # game's code runs
    # The ARM7's own image, not its WRAM, and deliberately not all of it. The
    # recorder is a ring of the last visits *to the window*, so the window is the
    # depth control: a console that spins (a leave4 tail was one self-branching
    # block at 06001F94, and the diag ROM's own ARM7 loop records ~4,000 visits
    # per emulated frame) fills any printout in a fraction of one frame.
    #
    # The leave5 run then measured where those rows go, band by band, in the
    # 3,072-row tail of a leave: p2 spent 2,375 of its rows in 0600F000 (the
    # transceiver teardown's busy polls), 289 in 0600E000, 132 in 06018000,
    # 125/122 in 06009000/06008000 - and none at all in 06015000-06016000, which
    # is where the two arms `pc7=06015528 lr7=06015518` (every guest) and
    # `pc7=06015530 lr7=0601552C` (the host) sit. 3,072 rows were about two
    # emulated frames of that, so the arm this instrument exists for was never
    # printed while the band list proved the band had been entered.
    #
    # So the window is the decision itself, taken from the disassembly the leave5
    # run left on disk (`logs/mkdsleave5_arm7code_leave1_*`): the routine at
    # 0601536C and the callee every guest arms through, 06015220, called from
    # 06015514. Between them sits the comparison this instrument is chasing -
    # `06015518 ldrh r0,[r9,#0x0C]; 0601551C cmp r0,#1; 06015520 bne 0601553C` -
    # with the host's own arm one call further on at 06015528. Both call sites,
    # the test, and the taken branch's target are inside; so is `r0` on every row
    # after the load, which is the value the driver read there.
    #
    # What the window leaves out is named rather than implied: the host's second
    # arm callee is 0600E824, and 0600Exxx is teardown traffic (289 rows of p2's
    # own tail), so it stays outside - the wireless write record's `lr` already
    # attributes that arm, and widening to 0600Exxx would spend the printout on
    # registers instead of on the branch. The boot/init and park below 06015200
    # stays out for the same reason.
    ARM7_LO, ARM7_HI = 0x06015200, 0x06015600

    def trace_arm(self, p, cpu, lo, hi):
        return self.ch.call(cmd="pctrace", player=p, cpu=cpu, lo=f"{lo:08X}", hi=f"{hi:08X}")

    def trace_report(self, p, cpu):
        return str(self.ch.call(cmd="pctrace", player=p, cpu=cpu))

    def mem(self, p, addr, length, cpu=9):
        out = self.ch.call(cmd="mem", player=p, addr=f"{addr:08X}", len=length, cpu=cpu)
        return [int(w, 16) for w in out["words"]]

    @staticmethod
    def trace_visits(report):
        """The visit rows of a pctrace report: (pc lr cpsr r0 r1 r2 r3).

        A visit row is seven hex words; a code-region row is one and every other
        line is prose, so this separates them by shape rather than by position.
        """
        rows = []
        for ln in report.splitlines():
            f = ln.split()
            if len(f) == 7 and all(len(v) == 8 for v in f):
                rows.append(tuple(int(v, 16) for v in f))
        return rows

    def dump_traces(self, players, cpu, label, window=0x400):
        """Write each console's own code path, and the code under its last PC.

        The full report goes to its own file (it is up to 256 KiB); the route log
        gets the head, the tail and the code window, which is what a reader needs
        to follow the branch the console was in when it decided.
        """
        for p in players:
            rep = self.trace_report(p, cpu)
            path = os.path.join(ROOT, f"logs/{self.tag}_pctrace_{label}_p{p}.txt")
            with open(path, "w") as fh:
                fh.write(rep)
            rows = self.trace_visits(rep)
            lines = rep.splitlines()
            self.say(f"--- arm{cpu} trace p{p}: {len(rows)} visit(s) -> logs/{os.path.basename(path)}")
            for ln in lines[:2] + lines[-4:]:
                self.say("    " + ln.strip()[:150])
            if not rows:
                continue
            pc = rows[-1][0]
            base = (pc - window // 2) & ~3
            words = self.mem(p, base, window, cpu=cpu)
            cpath = os.path.join(ROOT, f"logs/{self.tag}_code_{label}_p{p}.txt")
            with open(cpath, "w") as fh:
                fh.write(f"# {self.tag} {label} p{p} arm{cpu} code at last traced pc {pc:08X}\n")
                for i in range(0, len(words), 4):
                    fh.write(f"{base + i * 4:08X} " + " ".join(f"{w:08X}" for w in words[i:i + 4]) + "\n")
            self.say(f"    last pc {pc:08X} lr {rows[-1][1]:08X} r0 {rows[-1][3]:08X} "
                     f"r1 {rows[-1][4]:08X} r2 {rows[-1][5]:08X} r3 {rows[-1][6]:08X}; "
                     f"{window} bytes at {base:08X} -> logs/{os.path.basename(cpath)}")

    # The two places an ARM7 driver keeps its state: main RAM (0x02000000, the
    # mirror at 0x02800000 included) and its own 64 KiB WRAM. A base register
    # outside both is not a structure pointer, and reading behind one is a read of
    # whatever the address happens to alias.
    ARM7_RAM = ((0x02000000, 0x02400000), (0x02800000, 0x02C00000),
                (0x037F0000, 0x03810000))

    def dump_arm7_state(self, p, label, frame):
        """The ARM7's own register file, and the words its base registers point at.

        The register table names `pc7`/`lr7` of the arm itself, but not the value
        the comparison before it ran on: that lives in the register file, and
        `probe` is the reader that returns all sixteen (the scheduled samples are
        8 frames apart, and the driver is past the branch by the next one). The
        words at each callee-saved base register are read in the same breath
        because the structure the branch tests is pointed at by one of them, and a
        pointer read after the session reads a different run's driver state.
        """
        out = self.ch.call(cmd="probe", player=p)
        regs = out.get("regs7") or []
        names = ["r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9",
                 "r10", "r11", "r12", "sp", "lr", "pc"]
        hexregs = [int(v, 16) for v in regs]
        self.say(f"    f{frame} p{p} arm7 " + " ".join(
            f"{n}={v:08X}" for n, v in zip(names, hexregs)))
        if len(hexregs) < 16:
            # Said out loud: a `probe` that came back without the ARM7 register
            # file would otherwise look like a console whose base registers are
            # all outside RAM (the base-register reads below are skipped either
            # way, so silence here is the one way this dump can fail unseen).
            self.say(f"    f{frame} p{p} probe returned {len(hexregs)} of 16 arm7 "
                     f"registers - no base-register reads this leave")
            return
        for n in (4, 5, 6, 7, 9):
            base = hexregs[n] & ~3
            if not any(lo <= base < hi for lo, hi in self.ARM7_RAM):
                continue
            words = self.mem(p, base, 0x40, cpu=7)
            rows = [" ".join(f"+{4 * i:02X}:{w:08X}" for i, w in enumerate(words[j:j + 4]))
                    for j in range(0, len(words), 4)]
            self.say(f"        p{p} [r{n}+] " + " | ".join(rows))
            cpath = os.path.join(
                ROOT, f"logs/{self.tag}_arm7state_{label}_p{p}_r{n}.txt")
            with open(cpath, "w") as fh:
                fh.write(f"# {self.tag} {label} p{p} frame {frame} arm7 r{n}={hexregs[n]:08X} "
                         f"pc7={hexregs[15]:08X} lr7={hexregs[14]:08X}\n"
                         + " ".join(f"{v:08X}" for v in hexregs) + "\n")
                for row in rows:
                    fh.write(row + "\n")

    def dump_wifiio(self, players, label):
        """Each console's wireless block as it stands at the leave, to a file.

        The report's own `last writer per offset` table carries the ARM7 pc/lr of
        the last write to every wireless register, so a slot this console armed
        to take itself out of the room is named - pc, lr and the value written -
        if it was the last write there. That is why this is read at the moment
        the bus loses the console instead of minutes later: the driver keeps
        arming slots, and the table only holds the newest write per offset.
        Comparing the value against the app log's own `txslot LOCn <- XXXX` line
        for this console says whether the arm named here is the deauth's.
        """
        for p in players:
            rep = self.wifi(p)
            path = os.path.join(ROOT, f"logs/{self.tag}_wifiio_{label}_p{p}.txt")
            with open(path, "w") as fh:
                fh.write(rep + "\n")
            rows = [ln.strip() for ln in rep.splitlines()
                    if re.match(r"^(090|094|0A0|0A4|0A8)\b", ln.strip())]
            self.say(f"--- wireless block p{p} -> logs/{os.path.basename(path)}")
            for row in rows:
                self.say("    " + row)
            self.dump_arm7_code(p, label, rows)

    def dump_arm7_code(self, p, label, rows):
        """The ARM7 code under each wireless write the register table names.

        `rows` are the wireless-block lines the report echoes; an arm-slot row is
        `<off> <name> <val> <pc7> <lr7>` and every other line lacks the two
        trailing addresses, so the code is read only where they parse. `pc7` is
        the raw `ARM7.R[15]` at the write: exact on the interpreter, block-local
        under the shipped JIT (where `R[15]` is the running block's, not each
        instruction's - `pctrace` is the instrument that reports true instruction
        addresses), so the window is a symmetric one around it rather than a
        claim about which instruction stored. A symmetric window puts the store,
        the test above it and the call between them in one file either way, which
        is the pair that says whether the arm came from the driver's steady path
        or from a decision. Read at the moment the bus loses the console, because
        the driver image is not in any log and a window read after the session
        belongs to a different run.
        """
        seen = set()
        for row in rows:
            f = row.split()
            if len(f) < 5:
                continue
            try:
                pc = int(f[-2], 16)
            except ValueError:
                continue
            if pc == 0 or pc in seen:
                continue
            seen.add(pc)
            base = (pc - 0x200) & ~3   # aligned: `mem` reads 32-bit words
            words = self.mem(p, base, 0x400, cpu=7)
            cpath = os.path.join(
                ROOT, f"logs/{self.tag}_arm7code_{label}_p{p}_at{pc:08X}.txt")
            with open(cpath, "w") as fh:
                fh.write(f"# {self.tag} {label} p{p} arm7 code around the wireless "
                         f"write pc7 {pc:08X}\n# the row that named it: {row}\n")
                for i in range(0, len(words), 4):
                    fh.write(f"{base + i * 4:08X} "
                             + " ".join(f"{w:08X}" for w in words[i:i + 4]) + "\n")
            self.say(f"    arm7 code around {pc:08X} -> logs/{os.path.basename(cpath)}")

    def hold_room(self, players, frames, trace=False, sample_every=8, arm=True):
        """Hold a coupled room with no input at all, and instrument its leave.

        The experiment the press step cannot run: nothing is pressed and only
        emulated frames advance, so whatever ends the room is the games' own
        state. Sampling is per console every `sample_every` frames (each sample is
        a `probe` plus a `wifiio`, so a per-frame read of four consoles is not on
        offer); the bus mask is read on every pass *before* the sampling, so the
        frame the room ends is exact and the leave is noticed as soon as the emulated
        clock allows. With `trace`, every console's ARM7 is traced across the
        window over the driver's own image (`ARM7_LO..ARM7_HI`), the recorder is
        re-armed on the consoles still on the bus as soon as one console's own
        radio drops - so the second leaver's arm is recorded by a recorder whose
        window is the one the register ledger named - and the report and the code
        under every wireless write are read back at the leave.

        A per-16-frame code-region diff used to run here as well. It is gone, and
        with it the parser that produced it: the series it fed ("the ARM9 enters
        no new code region") is withdrawn in compatibility.md 2026-10-16, and a
        wider tail made each read of it a 200 KB report per console per bucket for
        a question nothing was going to act on.
        """
        coupled = coupled0 = self.mask()
        f0 = self.frames(players)
        page = {p: self.page(p) for p in players}
        nexts = {p: f0[p] for p in players}
        samples = {p: [(f0[p], page[p][1][:8], "%s/%db" % self.handed(p), self.role(p))] for p in players}
        self.say(f"=== HOLD: {frames} emulated frames, no input at all, from frame {f0[1]} "
                 f"mask={coupled:#x} | p1 {self.state(1)} | "
                 + " | ".join(f"p{p} {self.state(p)}" for p in players[1:]))
        leaves = []
        if trace and arm:
            for p in players:
                self.trace_arm(p, 7, self.ARM7_LO, self.ARM7_HI)
            self.say(f"trace armed on every console: arm7 [{self.ARM7_LO:08X},{self.ARM7_HI:08X})"
                     f" from frame {self.frames(players)[1]}")
        elif trace:
            # The ring is *not* cleared, and that is the point: the arm this
            # instrument is chasing on the host happens at the coupling, before
            # the hold starts (leave6 measured the host entering the window 0
            # times across a whole 119-frame hold while its own slot-2 arm row
            # still named 06015530), so arming here would keep the ring shallow
            # and drop the very pass being chased.
            self.say(f"arm7 trace kept as armed earlier: "
                     f"[{self.ARM7_LO:08X},{self.ARM7_HI:08X}) - the ring already holds "
                     f"the coupling's own passes")
        armed_cpu = 7
        while True:
            # The bus mask is read *before* the per-console sampling, not after:
            # the report's visit tail is a few emulated frames deep, so the frames
            # between a leave and the read are the whole margin, and one sample
            # (four consoles' pages, handed frames and roles) is several control
            # round trips of it. In the other order the read landed ~3 frames
            # after the leave, which put the leave's own arm outside the printout
            # (2026-10-16).
            cur = self.frames(players)
            elapsed = min(cur[p] - f0[p] for p in players)
            mask = self.mask()
            if mask != coupled:
                f_leave = cur[1]
                leavers = tuple(p for p in players
                                if (coupled & (1 << (p - 1))) and not (mask & (1 << (p - 1))))
                self.say(f"*** BUS LEFT {coupled:#x} at frame {f_leave}: mask={mask:#x} "
                         f"({elapsed} emulated frames after coupling), leavers {leavers}")
                leaves.append((f_leave, mask))
                if trace:
                    # The trace is read first, and the consoles that just left are
                    # read before the ones that stayed, because both of those are
                    # the margin: the report's visit tail is only a few emulated
                    # frames of this ARM7, so every control round trip between the
                    # leave and the read spends it - and the wireless table the
                    # other dumps read holds each offset's newest write and still
                    # holds the leave's own arm minutes later. Reading the trace
                    # last, after four wireless reports, four register dumps, four
                    # state reads and four page reads, is what put the previous
                    # pass's last traced visit in the idle loop instead of in the
                    # frames that decided (2026-10-16).
                    rest = tuple(p for p in players if p not in leavers)
                    self.dump_traces(leavers + rest, armed_cpu, f"leave{len(leaves)}")
                for p in players:
                    self.say(f"    f{f_leave} p{p} {self.state(p)} page {self.page(p)[1][:8]}")
                for p in players:
                    self.dump_arm7_state(p, f"leave{len(leaves)}", f_leave)
                if trace:
                    self.dump_wifiio(players, f"leave{len(leaves)}")
                    still = [p for p in players if mask & (1 << (p - 1))]
                    if still:
                        # Re-armed, not switched: a recorder cleared at the first
                        # leave holds exactly the window the *second* leaver's own
                        # arm sits in, and its window has to be the one the
                        # register ledger named (the driver's image).
                        f0 = cur
                        for p in still:
                            self.trace_arm(p, armed_cpu, self.ARM7_LO, self.ARM7_HI)
                        self.say(f"arm{armed_cpu} trace re-armed on the consoles still on the "
                                 f"bus {still}: [{self.ARM7_LO:08X},{self.ARM7_HI:08X})")
                        coupled = mask
                        continue
                if mask == 1:
                    break
                coupled = mask
                f0 = cur
                if len(leaves) >= 3:
                    break
            if elapsed >= frames:
                self.say(f"=== HOLD END at frame {cur[1]} ({elapsed} emulated frames, {frames} asked), "
                         f"mask={self.mask():#x}")
                # A hold that ends with the bus still up is not a hold that never
                # ends, and the trace has to be readable either way: the leave
                # this route watches for comes *after* the window in a
                # two-console room (measured 2026-10-16v: the guest's own walk and
                # deauth land ~1,080 emulated frames after coupling against ~325
                # with three clients), and a hold budget shorter than that left
                # the reading to a scratch probe whose own pctrace call re-armed
                # the recorder and destroyed it. Dumping here costs one report per
                # console and needs no second session.
                if trace:
                    self.dump_traces(players, armed_cpu, "holdend")
                break
            for p in players:
                if cur[p] >= nexts[p]:
                    nexts[p] = cur[p] + sample_every
                    # `handed()` and `role()` are two parses of one report, so
                    # this is two wireless-block reads per sample per console -
                    # a known waste, left as the shape the measured runs used
                    # (2026-10-16m) rather than changed blind.
                    pg = self.page(p)
                    fc, ln = self.handed(p)
                    role = self.role(p)
                    if (f"{fc}/{ln}b", role) != samples[p][-1][2:]:
                        samples[p].append((cur[p], pg[1][:8], f"{fc}/{ln}b", role))
                        self.say(f"f{cur[p]} p{p} -> handed={fc}/{ln}b role={role} page {pg[1][:8]}")
                    page[p] = pg
            time.sleep(0.02)
        for p in players:
            self.say(f"p{p} samples {len(samples[p])} | last "
                     + " | ".join(f"f{f} handed={fc} role={r} page={pg}"
                                  for f, pg, fc, r in samples[p][-3:]))
        self.say(f"HOLD VERDICT: coupled={coupled0:#x} bus steps={leaves} final mask={self.mask():#x}")
        return leaves

    def press_room(self, players, watch=300, sample_every=8):
        """Press each console's own control once, joiners first, then watch the room.

        The arm this route was missing. The default press arm lands on coordinates
        the route already knew (CUT OFF, VS, A) and presses the host only, so it
        cannot answer "the title wanted an input nobody sent": the guests' own
        walk to 48/64 and deauth 130-200 emulated frames after coupling
        (compatibility.md 2026-10-16w) happens with nothing pressed on them. Here
        every console is driven through the product's own input surface *where its
        own screen draws a control*, and the joiners go before the host because
        their leave is the thing under test. Every wait is the app's own emulated
        frame clock and every press is gated on that console's own bus bit, so a
        console whose radio has already dropped is reported as gone, not pressed;
        the bus mask is sampled between the per-console reads so the frame the room
        ends stays exact.

        The window is what bounds the reads: 131-197 emulated frames at four
        consoles is about 5 wall seconds (44% of DS speed), and one read is a shot
        plus up to `CONTROL_BANDS` band measurements per console - which is why the
        joiners are pressed first, why no console is read twice before it is
        pressed, and why the readings on the other side of the presses happen after
        all of them (with the frame each press landed on printed, so a press that
        missed the window is visible as one).
        """
        host = players[0]
        order = [p for p in players if p != host] + [host]
        f0 = self.frames(players)
        coupled = coupled0 = self.mask()
        self.say(f"=== PRESS-ROOM: one press on each console's own control, joiners first, "
                 f"from frame {f0[1]} mask={coupled:#x} | "
                 + " | ".join(f"p{p} {self.state(p)}" for p in players))
        pressed = {}
        for p in order:
            if not (self.mask() & (1 << (p - 1))):
                self.say(f"  p{p} is not on the bus any more: nothing pressed on it")
                continue
            before, ctl = self.press_console_control(p)
            pressed[p] = (before, ctl)
            # A couple of emulated frames between consoles: enough for the touch to
            # land on the console it was aimed at, not enough to spend the window.
            self.wait_frames(4, players)
        frame_pressed = self.frames(players)
        elapsed_press = min(frame_pressed[p] - f0[p] for p in players)
        self.say(f"=== PRESS-ROOM: every console read and pressed in {elapsed_press} emulated "
                 f"frames (frame {frame_pressed[1]}, mask={self.mask():#x} right after them), "
                 + ", ".join(f"p{p} {'its own control' if ctl else 'no control'}"
                             for p, (_b, ctl) in pressed.items()))
        for p, (before, ctl) in pressed.items():
            self.read_after_press(p, before, ctl is not None)
        nexts = {p: f0[p] for p in players}
        last = {p: None for p in players}
        leaves = []
        while True:
            cur = self.frames(players)
            elapsed = min(cur[p] - f0[p] for p in players)
            mask = self.mask()
            if mask != coupled:
                leavers = tuple(p for p in players
                                if (coupled & (1 << (p - 1))) and not (mask & (1 << (p - 1))))
                self.say(f"*** BUS LEFT {coupled:#x} at frame {cur[1]}: mask={mask:#x} "
                         f"({elapsed} emulated frames after the first press), leavers {leavers}")
                for p in players:
                    self.say(f"    f{cur[1]} p{p} {self.state(p)} "
                             f"page {self.page(p)[1][:8]}")
                leaves.append((cur[1], mask))
                coupled = mask
                if mask == 1 or len(leaves) >= 3:
                    break
            if elapsed >= watch:
                self.say(f"=== PRESS-ROOM WATCH END at frame {cur[1]} ({elapsed} emulated frames, "
                         f"{watch} asked), mask={self.mask():#x}")
                break
            for p in players:
                if cur[p] >= nexts[p]:
                    nexts[p] = cur[p] + sample_every
                    pg = self.page(p)
                    fc, ln = self.handed(p)
                    now = (pg[1][:8], f"{fc}/{ln}b", self.role(p))
                    if now != last[p]:
                        last[p] = now
                        self.say(f"f{cur[p]} p{p} -> handed={fc}/{ln}b role={now[2]} "
                                 f"page {now[0]}")
            time.sleep(0.02)
        for p in players:
            self.read_page(p, "the page after its own control was pressed")
        self.say(f"PRESS-ROOM VERDICT: coupled={coupled0:#x} bus steps={leaves} "
                 f"final mask={self.mask():#x}")
        return leaves


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=DEFAULT_ROM)
    ap.add_argument("--players", type=int, default=4)
    ap.add_argument("--attach", action="store_true",
                    help="attach to a session that is already running (do not launch one)")
    ap.add_argument("--tag", default="mkds4p")
    ap.add_argument("--port", type=int, default=7373)
    ap.add_argument("--boot-frames", type=int, default=900)
    ap.add_argument("--watch", type=int, default=360, help="emulated frames to watch after the press")
    ap.add_argument("--hold", type=int, default=0,
                    help="hold the coupled room with NO input at all for N emulated frames instead "
                         "of pressing CUT OFF/VS (the no-press arm of the leave experiment)")
    ap.add_argument("--trace-leave", action="store_true",
                    help="with --hold: trace every console's own code across the window and write "
                         "the leaving console's path and the code under its last PC to logs/")
    ap.add_argument("--press-room", action="store_true",
                    help="instead of pressing CUT OFF/VS/A at fixed coordinates, read each "
                         "console's own screen and press the control that console is drawing "
                         "(joiners first, one press each), then watch the room with --watch")
    ap.add_argument("--trace-early", action="store_true",
                    help="with --hold --trace-leave: arm the ARM7 trace window before the walk, so the "
                         "ring holds the coupling's own passes (the host arms its slot at the create, "
                         "not during the hold) instead of only what the hold sees")
    # The window is the depth control and the question decides it: the default
    # covers the two arms and the branch between them (see ARM7_LO), while the
    # driver's frame builders - 060135F8/0601364C/060136D0/06013784, named from
    # the image's own `mov r0,#0xC0` (the one deauth-frame control word in it) -
    # are a different question: which *caller* decided, read from the lr of the
    # visit that reaches the builder. `--arm7-lo/--arm7-hi` is that knob.
    ap.add_argument("--arm7-lo", default=None, help="ARM7 trace window low (hex), default the decision code")
    ap.add_argument("--arm7-hi", default=None, help="ARM7 trace window high (hex, exclusive)")
    args = ap.parse_args()

    if args.arm7_lo is not None or args.arm7_hi is not None:
        # Both bounds or neither, and never an empty or unreachable window: the trace
        # report says "0 visit(s)" for a window that was never armed, or that no ARM7
        # address can fall into, as readily as for code that never ran, so each of those
        # has to fail loudly rather than record nothing. A bare `--arm7-lo ''` enters this
        # block too (it is a bound the operator asked for, not an absent one) and is
        # rejected as an incomplete range, so a typo cannot quietly leave the default
        # window armed while the run looks like it was aimed somewhere else.
        if not args.arm7_lo or not args.arm7_hi:
            print("--arm7-lo and --arm7-hi are one range; give both", file=sys.stderr)
            return 2
        try:
            arm7_lo, arm7_hi = int(args.arm7_lo, 16), int(args.arm7_hi, 16)
        except ValueError:
            print(f"--arm7-lo/--arm7-hi must be hex addresses, got "
                  f"{args.arm7_lo!r}/{args.arm7_hi!r}", file=sys.stderr)
            return 2
        if arm7_hi <= arm7_lo:
            print(f"--arm7-hi ({arm7_hi:#x}) must be above --arm7-lo ({arm7_lo:#x})",
                  file=sys.stderr)
            return 2
        if arm7_hi > 0x100000000:
            print(f"--arm7-lo/--arm7-hi are 32-bit ARM7 addresses, got "
                  f"{arm7_lo:#x}/{arm7_hi:#x}", file=sys.stderr)
            return 2
        Runner.ARM7_LO, Runner.ARM7_HI = arm7_lo, arm7_hi

    players = tuple(range(1, args.players + 1))
    lines = []
    app_log = os.path.join(ROOT, f"logs/{args.tag}_app.log")
    route_log = os.path.join(ROOT, f"logs/{args.tag}_route.log")

    if not args.attach:
        env = dict(os.environ, MD_AUTOSTART_PLAYERS=str(args.players), MD_AUTOSTART_ROM=args.rom,
                   MD_LINK_VERBOSE="1", RUST_LOG="info", MD_LOG=app_log,
                   MD_CONTROL_PORT=str(args.port))
        out = subprocess.run([os.path.join(ROOT, "tools/run_app.sh")], env=env, cwd=ROOT,
                             capture_output=True, text=True)
        for line in out.stdout.splitlines():
            if line.startswith(("launched", "control:", "autostart")):
                print(line, flush=True)
        if out.returncode != 0:
            print(f"run_app.sh failed: {out.stderr.strip()}", file=sys.stderr)
            return 2

    for _ in range(180):
        try:
            r = Runner(args.port, app_log, args.tag, lines)
            break
        except SystemExit:
            time.sleep(1.0)
    else:
        print("control channel never came up", file=sys.stderr)
        return 2

    r.say(f"session: {args.players} console(s), rom {os.path.basename(args.rom)}"
          + (" (attached)" if args.attach else ""))
    r.say(f"boot: waiting {args.boot_frames} emulated frames on every console")
    r.wait_frames(args.boot_frames, players)
    for p in players:
        r.say(f"boot p{p} d={r.digests((p,))[p]} {r.state(p)}")
    if args.trace_early:
        # Before the host creates the group, so the ring spans the coupling: the
        # window is rare (9 rows in 105 frames of a leave), so the whole route
        # fits in it, while arming at the hold would start the ring after the
        # passes that decide.
        for p in players:
            r.trace_arm(p, 7, r.ARM7_LO, r.ARM7_HI)
        r.say(f"arm7 trace armed before the walk on every console: "
              f"[{r.ARM7_LO:08X},{r.ARM7_HI:08X}) from frame {r.frames(players)[1]}")

    reached, path, stuck = r.walk(1, r.in_section)
    r.say(f"section page: {reached} | {r.state(1)} | " + " -> ".join(f"{a}:{b[1][:8]}>{c[1][:8]}" for a, b, c in path)
          + (" | STOPPED at 0x1 bit or no room frame" if not reached else ""))
    if stuck:
        r.say(f"p1 stuck on page {stuck[1][:8]}: every input exhausted, radio idle - naming it")
        r.read_page(1, f"stuck page {stuck[1][:8]}")

    # Where the joiners are before the group exists: the joint job of the host's
    # creating press is to move the *other* consoles' lists, so this is the
    # baseline for that change.
    pre_create = {p: r.page(p) for p in players} if reached else {}
    if reached:
        r.read_page(1, "the section page, before the group exists")
        r.create_room(1, others=players[1:])

    # The host's own touch screen is the second, independent signal for a join:
    # CUT OFF exists only while a second racer is in the room, so this patch is
    # empty blue with one racer and carries a button with two. Sampled before any
    # joiner can have landed, so the comparison is a change, not a threshold.
    offer_base = None
    if reached:
        try:
            offer_base = r.luma(1, CUT_OFF_RECT)
            r.say(f"host CUT OFF patch reads {offer_base:.0f} with one racer (the patch is empty)")
        except SystemExit as err:
            r.say(f"cannot read the host's own touch screen: {err}")

    joined = []
    if reached:
        listed = []
        for p in players[1:]:
            ok, stop, jpath = r.join_route(p)
            r.say(f"joiner p{p} on its own group list: {ok} | {r.state(p)} | "
                  + " -> ".join(f"{a}:{b[1][:8]}>{c[1][:8]}" for a, b, c in jpath))
            if ok:
                listed.append(p)
            elif stop:
                r.say(f"p{p} stopped on page {stop[1][:8]}: radio idle after every join input")
                r.read_page(p, f"the page p{p} stopped on")
            r.say(f"bus after walking p{p}: mask={r.mask():#x}")
        # Only now does anyone take the room, and the taps all go out before any
        # of them is proved: a linked MKDS client leaves the lobby on its own, so
        # a joiner that joins while the next console is being verified is gone
        # before the last one arrives.
        r.say(f"{len(listed)} of {len(players) - 1} joiner(s) are on the group list "
              f"{listed} - tapping their bars back to back")
        for p in listed:
            r.tap_group_bar(p)
        for p in listed:
            ok = r.await_room(p)
            r.say(f"join p{p}: {ok} | {r.state(p)}")
            if ok:
                joined.append(p)
            else:
                r.read_page(p, f"the page p{p} was on when the room was not taken")
            r.say(f"bus after p{p}: mask={r.mask():#x}")

    if offer_base is not None:
        try:
            now = r.luma(1, CUT_OFF_RECT)
            # A hint, not a verdict: the patch is where CUT OFF is drawn, but the
            # light it moves (measured 103 -> 115 on a room holding its guests) is
            # about as large as the room redrawing itself, so what the host's own
            # screen *says* is read out below rather than inferred from this.
            r.say(f"host CUT OFF patch reads {now:.0f} with the joiners in (was {offer_base:.0f})")
        except SystemExit as err:
            r.say(f"cannot re-read the host's own touch screen: {err}")
        # The host's own room page, before the lobby is closed: this is the other
        # console's account of the same join, and it is the one that says whether
        # the room holds the racers the joiners' own state says they are.
        r.read_page(1, "the host's own room page with the joiners in it")

    if reached and joined and args.hold:
        # The other arm of this route's own experiment: the room is left alone.
        r.hold_room(players, args.hold, trace=args.trace_leave, arm=not args.trace_early)

    if reached and joined and not args.hold:
        # The lobby closes the moment the racers are in: CUT OFF exists on the
        # host's own screen only while a second racer is listed, and a guest that
        # is left waiting leaves on its own - measured with no input at all at
        # **167 emulated frames** after the room couples (2026-10-16l), so this
        # press lands *inside* that window, not before it. Holding the room
        # untouched is not an option this title gives; the guest's own leave is
        # the thing to solve, and it is inside MKDS's own code. Three more
        # no-press runs since (2026-10-16m, `--hold --trace-leave`) left 107/115/148
        # emulated frames after their traces armed - same order, never the hold's
        # own budget - and cleared the answer path of any divergence (every answer
        # carries its own arm's body; the guests' own games arm one body each).
        if args.press_room:
            # The other arm over the same room: what each console is given is a
            # reading of its own screen, so a control nobody's page draws cannot
            # be pressed by accident and a page that draws none is reported. Run at
            # four consoles (2026-10-16x) it answered the question the fixed-coordinate
            # arm above could not: the joiners' own pages draw **no control at all**
            # (a racer list above the lower half and a 5-row panel), the host's own
            # control is found and pressed from its pixels (148,170), and the guests'
            # own leave lands in the same window as `--hold`'s no-press baseline - so
            # nothing this title offers a client to press is what ends the room.
            r.press_room(players, watch=args.watch)
        else:
            for label, xy in (("cut-off", CUT_OFF_TAP), ("vs", VS_TAP)):
                r.ch.call(cmd="touch", player=1, x=xy[0], y=xy[1], ms=150)
                r.ch.call(cmd="untouch", player=1)
                r.wait_frames(120, (1,))
                r.say(f"{label} pressed on the host: {r.state(1)} d={r.page(1)[1][:8]}")
            # The race's own confirm, on every console that is in the room: the
            # documented two-console route presses A on the host after VS.
            for p in [1] + joined:
                r.ch.call(cmd="press", player=p, buttons=1, ms=180)
            r.wait_frames(120, players)
            for p in players:
                r.read_page(p, "the page after VS")

    # `--press-room` runs its own watch (it needs the bus mask between the
    # per-console reads to keep the frame the room ends exact), so the tail's
    # per-60-frame rows would be a second, coarser census of the same window.
    for i in range(0 if args.press_room else args.watch // 60):
        r.wait_frames(60, players)
        row = []
        for p in players:
            fb = r.ch.call(cmd="probe", player=p)["framebuffer"]["bottom"]
            row.append(f"p{p} mean={fb['mean']:.0f} blk={fb['black_pct']}% {r.state(p)} "
                       f"d={r.digests((p,))[p]}")
        r.say(f"watch {i + 1} (mask={r.mask():#x}): " + " | ".join(row))

    r.ch.call(cmd="shot", name=f"{args.tag}_end")
    r.say(f"VERDICT: section={reached} joined={joined} mask={r.mask():#x} "
          f"shots=logs/shots/{args.tag}_end.png")

    # The session this route launched is this route's to close. Without this the
    # four emulated consoles keep running at full tilt after the timeline is
    # written - measured 2026-10-16o: a four-console hold was still up and still
    # logging 15 minutes after its own route had returned, which is how a stale
    # session survives into the next pass and steals the control port. `--attach`
    # means the session was not this route's, so it is left alone.
    if not args.attach:
        # A quit is the one control call whose reply cannot arrive: the app closes
        # the control connection as it exits, so the wrapper's "the app closed the
        # control connection" is this call's *expected* outcome rather than a
        # failure - the second version of this code logged it as an error on every
        # successful shutdown (2026-10-16o). What proves the session died is the
        # log, and the supervisor's own lines only appear after the whole teardown
        # has run (they landed 12 ms after a fixed two-second wait the first time),
        # so they are polled for and reported, or their absence is said out loud.
        try:
            r.ch.call(cmd="quit")
        except SystemExit:
            pass
        status = []
        deadline = time.time() + 20.0
        while time.time() < deadline:
            time.sleep(0.5)
            try:
                with open(app_log, errors="replace") as fh:
                    status = [ln for ln in fh.read().splitlines()[-40:]
                              if ln.startswith("[supervisor]")]
            except OSError:
                break
            if status and "exited with status" in status[-1]:
                break
        for ln in status[-2:]:
            r.say(f"shutdown: {ln}")
        if "exited with status" not in " ".join(status):
            r.say("shutdown: the app log carries no exit-status line - whether the session "
                  "died cleanly is unknown from this log")

    with open(route_log, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"(timeline written to {route_log})")
    return 0 if (reached and len(joined) == len(players) - 1) else 1


if __name__ == "__main__":
    sys.exit(main())
