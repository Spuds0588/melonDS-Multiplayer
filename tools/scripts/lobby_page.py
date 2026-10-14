#!/usr/bin/env python3
"""Read one console's MPH lobby page out of a composite screenshot.

Each lobby page is recognised by its *own* controls rather than by how many
ladder steps have been taken. That distinction is the point of this file: the
host's tab-completion dots (three cyan dots under MODE / ARENA / HUNTER, one per
finished page) saturate at three, so the roster and every page after it read
alike, and a driver that retries on a dot count walks the host onto ADVANCED
OPTIONS instead of stopping. A page fingerprint cannot do that.

Measured on the 256x192 bottom screen of a console in tools/appctl.py's `shot`
composite (console N occupies x [(N-1)*256, N*256), bottom screen at y 192..383):

    MODE    the BATTLE disc's orange,      x 18..72   y 44..96    >= 600
    ARENA   the page's own green check,    x 205..223 y 163..183 >= 12
    HUNTER  the seven hunter discs,        x 4..252   y 124..150  >= 100
    ROSTER  the roster's own ADD BOT ring, x 216..254 y 148..186 >= 50
    INGAME  the match HUD's ink,           x 0..256   y 0..32     > 800
            *and* a colourful top screen,   x 0..256   y 0..192   >= 10000
    DIALOG  the bottom screen's HUD ink without a colourful top screen
    UNKNOWN nothing matched, or two pages matched - a frame caught mid-transition

The thresholds sit inside measured gaps, not at their edges. ARENA's tick reads
12 on the page and 0 on every other frame. Player 1's HUD reads 2730 ink pixels
and a guest's yellow HUD 1315..1336, against a lobby maximum of 430, so INGAME's
threshold sits at 800 - the guest HUD is why it is not 1500, which misread both
players of a live match as HUNTER.

The *order* of ADD BOT and the hunter-disc band is load bearing, and the frames
settled it: the ADD BOT ring's box (x 216..254, y 148..186) overlaps the disc band
(x 4..252, y 124..150) along two rows, so a roster with a bot added on it measured
164 orange pixels in the disc band against the ring's own 94 and read as HUNTER
(logs/shots/v9i_probe1.png, the host's roster right after the driver tapped ADD
BOT: `page=HUNTER pill=0 icons=164 addbot=94 discs=88`), which is exactly the
frame a driver's roster wait has to recognise. Every labelled HUNTER frame measures
`addbot=0` (0 of the 21 frames in logs/shots, including both guests' own pages in
the live three-console runs) and no page carrying the ring has ever read below 69,
so the ring is the more specific test and it runs first.

ROSTER is deliberately *not* "none of the above". The roster used to be the
fallback, and a frame with no page controls at all - the half-second between two
lobby pages - therefore read as the roster and made every ladder step believe the
host had already arrived: logs/or3_match.log shows the host's ARENA step reading
ROSTER one second after its own tap, the HUNTER step skipping itself because of
it, and the host left sitting on its hunter page for the rest of the run. ADD BOT
is positive evidence instead, and it is rock steady: 90 orange pixels on a
one-player roster, a two-player roster, the old F_start and a live one alike,
against 24 for the ARENA page and 0 for MODE, HUNTER and the transition frame
kept as logs/shots/ref_transition.png.

DIALOG exists because the ink probe alone is not a match. A message box over the
in-match HUD - "THERE ARE NOT ENOUGH PLAYERS TO CONTINUE" is the one MPH shows
when a guest's side of the handshake did not land - reads 2729 ink pixels against
a real match's 2730, because the bottom screen is the same HUD either way; only
the *top* screen differs, a 3D arena against a plain black dialog. Counting
colourful pixels there (channel spread > 40) separates them: **915-928** for the
dialog, against **6388-6551** for a match's own `LAWS OF BATTLE` briefing screen
(where the arena is dark and the text is the only bright thing), 16473-19485 for
play in progress and 32724 for a recorded one. The threshold is 3000, which is
between the two clusters rather than in the middle of either. It was 10000 at
first, taken from the in-play frames alone, and that made the oracle report a
*real* match as DIALOG for its first twenty seconds - so this file's numbers are
only as good as the labelled frames in `--selftest`, and ctl2 (a two-console
match's briefing, read by eye) is one of them now.

The START GAME pill (x 156..242, y 36..52) is only meaningful on the roster: its
highest non-roster reading is 144 (the ARENA page's sliders) against 413..878 for
a roster that lists a second player and 0 for one that does not. It is reported
alongside the page so a caller can require pill >= 300 before tapping it.

Usage:
    lobby_page.py SHOT.png COL            -> "page=ROSTER pill=413 ..."
    lobby_page.py --selftest              -> checks the labelled shots in logs/

COL is the **console number**, 1..N, the same counting `appctl.py --player N` and
the driver use, and a number outside the shot is an error rather than a read: the
composite is 256 px per console, so an out-of-range column used to crop to
nothing and report a black frame as `page=UNKNOWN pill=0 ...` - which is a page
the caller can act on (it reads as a transition) instead of a broken read. The
Python functions below index the console as `col` from 0; only the command line
is 1-based.
"""

import os
import sys

# (x0, y0, x1, y1) in bottom-screen coordinates, bottom screen at y 192.
DISCS = (18, 44, 72, 96)
ARENACHK = (205, 163, 223, 183)
ICONS = (4, 124, 252, 150)
ADDBOT = (216, 148, 254, 186)
PILL = (156, 36, 242, 52)
INKTOP = (0, 0, 256, 32)
TOPS = (0, 0, 256, 192)

DISC_MIN = 600
ARENACHK_MIN = 12
ICONS_MIN = 100
ADDBOT_MIN = 50
INKTOP_MIN = 800
TOPCOLOUR_MIN = 3000
PILL_MIN = 300

PAGES = ("MODE", "ARENA", "HUNTER", "ROSTER", "INGAME", "DIALOG", "UNKNOWN")


def orange(p):
    r, g, b = p
    return r > 150 and 50 < g < 200 and b < 110 and (r - b) > 80


def green(p):
    r, g, b = p
    return g > 140 and g - r > 40 and g - b > 30


def bright(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] > 150


def probes(shot, col):
    """The raw counts for console `col` (0-based) of a composite shot."""
    from PIL import Image

    im = Image.open(shot).convert("RGB")
    col = col * 256
    bot, top = im.crop((col, 192, col + 256, 384)), im.crop((col, 0, col + 256, 192))

    def count(img, box, ok):
        px, (x0, y0, x1, y1) = img.load(), box
        return sum(1 for y in range(y0, y1) for x in range(x0, x1) if ok(px[x, y]))

    return {
        "discs": count(bot, DISCS, orange),
        "arenachk": count(bot, ARENACHK, green),
        "icons": count(bot, ICONS, orange),
        "addbot": count(bot, ADDBOT, orange),
        "pill": count(bot, PILL, orange),
        "inktop": count(bot, INKTOP, bright),
        "topcolour": count(top, TOPS, lambda p: max(p) - min(p) > 40),
    }


def classify(d):
    """The page name, or UNKNOWN when the frame is not one this knows.

    The order is not cosmetic: the probes overlap (the MODE page's disc grid and
    the ARENA page's slider row both carry enough orange to trip the hunter-disc
    probe, 802 and 351 against a 100 threshold), so the tests run most-specific
    first and the first hit wins. ARENA's tick is the only *unique* probe in the
    set; requiring exactly one hit instead made MODE and ARENA both UNKNOWN.
    """
    if d["inktop"] > INKTOP_MIN:
        # The HUD's ink is on the bottom screen in a match *and* in a message box
        # drawn over the in-match HUD; only a match has the 3D view above it.
        return "INGAME" if d["topcolour"] >= TOPCOLOUR_MIN else "DIALOG"
    if d["arenachk"] >= ARENACHK_MIN:
        return "ARENA"
    if d["discs"] >= DISC_MIN:
        return "MODE"
    # ADD BOT before the hunter discs: the ring's box overlaps the disc band along
    # y 148..150, which is enough for a bot-bearing roster to read 164 there against
    # a 100 threshold (logs/shots/v9i_probe1.png). HUNTER frames carry no ring at
    # all, so this order cannot cost the hunter page anything.
    if d["addbot"] >= ADDBOT_MIN:
        return "ROSTER"
    if d["icons"] >= ICONS_MIN:
        return "HUNTER"
    return "UNKNOWN"


def describe(shot, col):
    d = probes(shot, col)
    return dict(d, page=classify(d))


def line(shot, col):
    r = describe(shot, col)
    return ("page={page} pill={pill} icons={icons} addbot={addbot} "
            "arenachk={arenachk} discs={discs} inktop={inktop} "
            "topcolour={topcolour}").format(**r)


# The labelled frames this classifier is checked against, all in logs/shots. Every
# one is a console whose page was established by eye, and the set deliberately
# includes the pages a *dot count* confuses (h2/t3/d1/B_joined are "three dots"
# and are not the roster) and the frames the old absence-based rule got wrong.
#
# hstep_* names are rewritten by every driver run, so the references that carry the
# weight are the ones a pass saved under their own name: F_start (the old working
# two-console roster), st1/sw (a live lobby with one and two players), stm2 (a live
# match 30 s in), mph2_match_a and healthy2 (two earlier matches), mph3_picks and
# ref_transition (both frames that the previous absence-based rule read as ROSTER).
EXPECTED = [
    ("F_start.png", 0, "ROSTER"),                # roster listing a second player
    ("st1.png", 0, "ROSTER"),                    # live lobby, one player, pill=0
    ("sw.png", 0, "ROSTER"),                     # live lobby, two players
    # The roster *with a bot added on it* - the frame the old order called HUNTER:
    # the ADD BOT ring's own rows overlap the hunter-disc band, so a roster that
    # lists a bot measured icons=164 against the ring's 94 (v0.1.9bk, the host's
    # roster one tap after ADD BOT; kept under its own name for this reason).
    ("v9i_probe1.png", 0, "ROSTER"),
    ("healthy2.png", 0, "INGAME"),               # the known two-console match HUD
    ("healthy2.png", 1, "INGAME"),
    ("ref_mode.png", 0, "MODE"),                 # MODE tab over the BATTLE disc grid
    ("ref_arena.png", 0, "ARENA"),               # ARENA sliders + green tick
    ("ref_transition.png", 0, "UNKNOWN"),        # caught between ARENA and HUNTER
    # The message box that a bottom-screen-only probe calls a match: the host's own
    # "THERE ARE NOT ENOUGH PLAYERS TO CONTINUE" from this pass's live attempt.
    ("stm.png", 0, "DIALOG"),
    ("stm2.png", 0, "DIALOG"),
    ("stm2.png", 1, "DIALOG"),
    # The other side of that threshold: a real two-console match's briefing screen,
    # whose dark arena reads 6388 colourful pixels against the dialog's 915.
    ("ctl2.png", 0, "INGAME"),
    ("ctl2.png", 1, "INGAME"),
]
# The pill is the roster's own join test, so it is asserted too: a roster that
# lists only the host offers nothing to tap, a two-player one does. "none" means
# *below PILL_MIN* rather than zero, because a page that is not the roster at all
# still has orange near that region (the ARENA sliders read 104). What has to hold
# is that only a second player's roster reaches the tap threshold.
PILL_EXPECTED = [
    ("st1.png", 0, "none"),                      # one player: no START GAME
    ("ref_transition.png", 0, "none"),           # not a roster at all
    ("F_start.png", 0, "present"),               # two players: START GAME
    ("sw.png", 0, "present"),                    # two players: START GAME
]


def selftest(shots="logs/shots"):
    bad = 0
    for name, col, want in EXPECTED:
        path = os.path.join(shots, name)
        if not os.path.exists(path):
            print(f"SKIP {name} (not present)")
            continue
        r = describe(path, col)
        got = r["page"]
        ok = got == want
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name} c{col}: page={got} want={want} {r}")
    for name, col, want in PILL_EXPECTED:
        path = os.path.join(shots, name)
        if not os.path.exists(path):
            print(f"SKIP {name} (not present)")
            continue
        r = describe(path, col)
        ok = r["pill"] >= PILL_MIN if want == "present" else r["pill"] < PILL_MIN
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name} c{col}: pill={r['pill']} want "
              f"{'>= ' + str(PILL_MIN) if want == 'present' else '< ' + str(PILL_MIN)}")
    print(f"{'FAIL' if bad else 'PASS'}: {len(EXPECTED) + len(PILL_EXPECTED)} labelled "
          f"checks, {bad} wrong")
    return 1 if bad else 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        return selftest()
    if len(argv) != 3 or not argv[1].endswith(".png"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    shot = argv[1]
    if not os.path.exists(shot):
        print("page=NOSHOT pill=0 icons=0 addbot=0 arenachk=0 discs=0 inktop=0")
        return 0
    from PIL import Image

    consoles = Image.open(shot).size[0] // 256
    if not argv[2].isdigit() or not 1 <= int(argv[2]) <= consoles:
        print(f"{shot} holds {consoles} console(s) at 256 px each; console "
              f"{argv[2]} is not one of them", file=sys.stderr)
        return 2
    print(line(shot, int(argv[2]) - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
