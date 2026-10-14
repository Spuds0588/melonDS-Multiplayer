#!/usr/bin/env bash
#
# sfc-dp-host.sh - drive Star Fox Command into its Download Play / Recruitment
# host screen, deciding progress from emulator and link state.
#
# Why this exists: single-card Download Play (Milestone 4) needs a host to talk
# to, and Star Fox's Recruitment is the ready-made one. Its menus are touch-only
# and its font defeated the ASCII screen readers (`appctl.py see`/`glyphs.py`),
# so this driver never reads text. It walks the screens by *touch geometry* -
# measured once off the framebuffer, not guessed - and it decides success only
# from the link: the console's own `role` and the frames leaving it.
#
# The walk is deliberately one repeated tap. Star Fox's touch targets happen to
# line up down the screen: the title screen accepts a touch anywhere, the main
# menu's second panel (Download Play) spans y 58..108, and the Download Play
# page's own option (Recruitment) spans y 51..141. (128, 96) lands inside all
# three, so the same tap advances whichever screen is showing, and the loop stops
# the moment the emulator reports the host role. A tap that lands during a screen
# transition costs one retry, not a wrong path.
#
# The measurements (256x192 bottom screen):
#   title screen        any touch advances; START does nothing
#   main menu           four panels, separators at y 57/109/161 - panel 2 is
#                       Download Play (measured: tapping y83 changes the page)
#   Download Play page  one large option panel spanning y ~51..141; tapping its
#                       centre raises the radio and enters Recruitment
#
# Success is `role=1` on this console AND this console's bit on the link bus AND
# 204-byte beacons leaving it (`fc=0080`). Near-misses report what they saw.
#
# Usage:
#   MD_AUTOSTART_PLAYERS=2 MD_LINK_VERBOSE=1 \
#     MD_AUTOSTART_ROM="$PWD/Test Roms/Star Fox Command (USA).nds" tools/run_app.sh
#   tools/scripts/sfc-dp-host.sh
#
# Overrides: PLAYER (default 1), TAP_X/TAP_Y (default 128/96), TRIES (default 8),
#            SETTLE_S (default 3), APP_LOG (default logs/sfc_dp_app.log),
#            CUT_X/CUT_Y (default 91/178), CLIENT_WAIT_S (default 60),
#            TRANSFER_WAIT_S (default 45).
#
# Exit codes are state-based, not "did anything happen":
#   0  the host role was reached AND the transfer's first data command (0x04)
#      was seen after Cut Off
#   2  the host role was reached but no 0x04 command followed Cut Off - the
#      parked state this driver exists to make reproducible
#   1  the host role was never reached
#
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

CTL_PORT="${CTL_PORT:-7373}"
CTL=(python3 tools/appctl.py --port "$CTL_PORT")
LOG="${LOG:-$PWD/logs/sfc_dp_host.log}"
APP_LOG="${APP_LOG:-$PWD/logs/sfc_dp_app.log}"
PLAYER="${PLAYER:-1}"
TAP_X="${TAP_X:-128}"
TAP_Y="${TAP_Y:-96}"
TRIES="${TRIES:-8}"
SETTLE_S="${SETTLE_S:-3}"

exec >>"$LOG" 2>&1
echo "--- sfc-dp-host start $(date +%H:%M:%S) pid $$ port $CTL_PORT player $PLAYER (app log $APP_LOG)"

say()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
run()  { say "ctl: $*"; "${CTL[@]}" "$@" 2>&1 | sed 's/^/    /'; }
tap()  { run touch "$2" "$3" --player "$1" --ms 120; run untouch --player "$1"; }
mask() { "${CTL[@]}" status 2>/dev/null | sed -n 's/.*link mask \(0x[0-9a-fA-F]*\).*/\1/p' | head -1; }
role() { "${CTL[@]}" wifiio --player "$1" 2>/dev/null | grep -o 'role=[0-9]*' | head -1; }
# How many of the host's beacons the joiner has heard, off the control channel.
# An earlier version grepped the app log for `link-trace] ... fc=0080`, a line
# the app no longer prints, so the check always read 0 and failed healthy runs
# (measured 2026-10-14: role=1, AID c001, transfer streaming, beacons "tx 0").
beacons() { "${CTL[@]}" dpjoiner 2>/dev/null | sed -n 's/.*beacons \([0-9]*\).*/\1/p' | head -1; }
bit()  { printf '%d' "$(( ($1 & (1 << ($2 - 1))) != 0 ))" 2>/dev/null || echo 0; }

# ---------------------------------------------------------------- session up
say "waiting for the session"
for i in $(seq 1 60); do
    if "${CTL[@]}" status 2>/dev/null | grep -q "player(s)"; then break; fi
    sleep 2
done
# Fail fast with no session: every later call would be a tap into a control
# channel that is not there, which reads as a driver that simply never succeeds.
if ! "${CTL[@]}" status 2>/dev/null | grep -q "player(s)"; then
    say "FAIL: no session on port $CTL_PORT after 120s - launch the app first (see the usage header)"
    exit 1
fi
say "session: $( "${CTL[@]}" status 2>/dev/null | head -1 )"

# ---------------------------------------------------------------- the walk
role_now="$(role "$PLAYER")"
beacons_before="$(beacons)"
say "before the walk: ${role_now:-role=0}, link mask $(mask), beacons $beacons_before"

attempt=0
while [ "$attempt" -lt "$TRIES" ]; do
    attempt=$((attempt + 1))
    say "attempt $attempt/$TRIES: ${role_now:-role=0} - tapping ($TAP_X,$TAP_Y)"
    tap "$PLAYER" "$TAP_X" "$TAP_Y"
    sleep "$SETTLE_S"
    role_now="$(role "$PLAYER")"
    if [ "$role_now" = "role=1" ]; then
        say "host role reached after $attempt tap(s): role=$role_now"
        break
    fi
done

# ---------------------------------------------------------------- the verdict
sleep 3
final_role="$(role "$PLAYER")"
final_mask="$(mask)"
beacons_after="$(beacons)"; beacons_after="${beacons_after:-0}"; beacons_before="${beacons_before:-0}"
onbus="$(bit "$final_mask" "$PLAYER")"
say "final: role=$final_role · link mask $final_mask (this console on bus: $onbus) · beacons heard $beacons_after (was $beacons_before)"

if [ "$final_role" != "role=1" ]; then
    say "FAIL: this console never took the host role after $TRIES tap(s) - it is not on the Recruitment screen"
    exit 1
fi
if [ "$beacons_after" -le "$beacons_before" ]; then
    say "FAIL: host role but the joiner heard no beacons - the waiting broadcast is not on the wire"
    exit 1
fi
say "PASS: console $PLAYER is the Recruitment host, beacons on the wire ($beacons_after, +$((beacons_after - beacons_before)))"

# ---------------------------------------------------------------- the lobby
# Reaching the host role only puts the console on Recruitment; the transfer
# does not begin until the host touches its own Cut Off button, and that button
# only exists once a client is listed. So the walk above is not the end of the
# route: wait for the joiner to be registered on the link, then tap Cut Off.
# The coordinate is measured, not guessed - the button is the left of the two at
# the bottom of the host's bottom screen (the right one is Back), and a tap just
# above it (y=167) does nothing.
CUT_X="${CUT_X:-91}"
CUT_Y="${CUT_Y:-178}"
CLIENT_WAIT_S="${CLIENT_WAIT_S:-60}"
TRANSFER_WAIT_S="${TRANSFER_WAIT_S:-45}"
cut() { run touch "$2" "$3" --player "$1" --ms 200; run untouch --player "$1"; }
# The class the host's game is asking, as the joiner's own counters read it.
lastcmd() { "${CTL[@]}" dpjoiner 2>/dev/null \
    | sed -n 's/.*host commands [0-9]* (last 0x\([0-9a-fA-F]*\).*/\1/p' | head -1; }

say "waiting up to ${CLIENT_WAIT_S}s for a client to be registered"
joined=0
for i in $(seq 1 "$CLIENT_WAIT_S"); do
    if "${CTL[@]}" dpjoiner 2>/dev/null | grep -q "AID 0x[1-9a-fA-F]"; then joined=1; break; fi
    sleep 1
done
if [ "$joined" = "1" ]; then
    say "joiner registered: $( "${CTL[@]}" dpjoiner 2>/dev/null | grep 'last assoc' )"
else
    say "no client registered in ${CLIENT_WAIT_S}s - tapping Cut Off anyway"
fi
say "Cut Off: tapping ($CUT_X,$CUT_Y) on console $PLAYER"
cut "$PLAYER" "$CUT_X" "$CUT_Y"

say "waiting up to ${TRANSFER_WAIT_S}s for the host to stream a 0x04 data command"
started=0
for i in $(seq 1 "$TRANSFER_WAIT_S"); do
    lc="$(lastcmd)"
    if [ "$lc" = "04" ]; then started=1; break; fi
    sleep 1
done
lc="$(lastcmd)"
say "after Cut Off: host command class 0x${lc:-??} · $( "${CTL[@]}" status 2>/dev/null | head -1 )"

if [ "$started" = "1" ]; then
    say "TRANSFER STARTED: the host left its waiting state and streamed 0x04 data commands"
    exit 0
fi
say "PARKED: host role reached and Cut Off tapped, but no 0x04 data command followed - the post-Cut-Off waiting state is reproducible from this one command"
exit 2
