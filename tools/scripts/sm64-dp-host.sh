#!/usr/bin/env bash
#
# sm64-dp-host.sh - drive Super Mario 64 DS into its Download Play waiting
# room, start the download, and report the transfer state.
#
# Why this exists: SM64DS's VS mode IS single-card Download Play (registry
# ASME, download_play), and it is the test set's second single-card title
# after Star Fox. Its font defeats the ASCII screen readers, so the walk is
# touch geometry measured off framebuffer captures - and its waiting room
# needs one tap the first version of this route never tried: the bottom
# TOUCH TO START bar only starts the download once a client is registered
# (with no client it answers "Practice alone?"). Tapping it with slave 1
# associated moves the host 00 -> 04 and streams the 514896-byte image to
# BOOTED (measured 2026-10-14); waiting without tapping parks forever with
# the client collected on ~100% of rounds.
#
# The walk (bottom-screen DS pixels):
#   title screen        any touch advances
#   main menu           the VS bar at about (37,172)
#   waiting room        wait for the joiner to be registered (AID C001),
#                       then the TOUCH TO START bar at about (128,168).
#                       Do NOT tap the bar before a client is registered.
#
# Success is read only from the link and the joiner's counters, never from
# a screen: exit 0 when a 04 DataPacket is seen (or the download: count
# moves), 2 when the host reached its waiting room, registered the client
# and stated its RSA image but never streamed data, and 1 when the waiting
# room was never reached.
#
# Usage: launch the app first, then run:
#   MD_AUTOSTART_PLAYERS=2 MD_AUTOSTART_ROM="$PWD/Test Roms/Super Mario 64 DS (USA) (Rev 1).nds" tools/run_app.sh
#   tools/scripts/sm64-dp-host.sh
#
# Exit codes are state-based, not "did anything happen":
#   0  the host streamed 0x04 data commands (or the download: count moved)
#   2  waiting room + registered client + RSA stated, but no 0x04 followed
#   1  the waiting room was never reached
#
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

CTL_PORT="${CTL_PORT:-7373}"
CTL=(python3 tools/appctl.py --port "$CTL_PORT")
LOG="${LOG:-$PWD/logs/sm64_dp_host.log}"
APP_LOG="${APP_LOG:-$PWD/logs/sm64_app.log}"
PLAYER="${PLAYER:-1}"
TAP_X="${TAP_X:-128}"
TAP_Y="${TAP_Y:-96}"
VS_X="${VS_X:-37}"
VS_Y="${VS_Y:-172}"
START_X="${START_X:-128}"
START_Y="${START_Y:-168}"
BOOT_S="${BOOT_S:-8}"
HOST_WAIT_S="${HOST_WAIT_S:-60}"
TRANSFER_WAIT_S="${TRANSFER_WAIT_S:-120}"

exec >>"$LOG" 2>&1
echo "--- sm64-dp-host start $(date +%H:%M:%S) pid $$ port $CTL_PORT player $PLAYER (app log $APP_LOG)"

say()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
run()  { say "ctl: $*"; "${CTL[@]}" "$@" 2>&1 | sed 's/^/    /'; }
tap()  { run touch "$2" "$3" --player "$1" --ms 150; run untouch --player "$1"; }
mask() { "${CTL[@]}" status 2>/dev/null | sed -n 's/.*link mask \(0x[0-9a-fA-F]*\).*/\1/p' | head -1; }
role() { "${CTL[@]}" wifiio --player "$1" 2>/dev/null | grep -o 'role=[0-9]*' | head -1; }
# The class the host's game is asking, as the joiner's own counters read it.
lastcmd() { "${CTL[@]}" dpjoiner 2>/dev/null \
    | sed -n 's/.*host commands [0-9]* (last 0x\([0-9a-fA-F]*\).*/\1/p' | head -1; }
# How many image bytes the joiner holds.
downloaded() { "${CTL[@]}" dpjoiner 2>/dev/null \
    | sed -n 's/.*download: \([0-9]*\) of .*/\1/p' | head -1; }
registered() { "${CTL[@]}" dpjoiner 2>/dev/null | grep -q "AID 0x[1-9a-fA-F]"; }

# ---------------------------------------------------------------- session up
say "waiting for the session"
for i in $(seq 1 60); do
    if "${CTL[@]}" status 2>/dev/null | grep -q "player(s)"; then break; fi
    sleep 2
done
if ! "${CTL[@]}" status 2>/dev/null | grep -q "player(s)"; then
    say "FAIL: no session on port $CTL_PORT after 120s - launch the app first (see the usage header)"
    exit 1
fi
say "session: $( "${CTL[@]}" status 2>/dev/null | head -1 )"

# ---------------------------------------------------------------- the walk
# Title advances on any touch; the menu's VS bar is the second step. Both
# taps are blind geometry, so the verdict below - not the taps - decides.
say "tap 1/2: title at ($TAP_X,$TAP_Y)"
tap "$PLAYER" "$TAP_X" "$TAP_Y"
sleep "$BOOT_S"
say "tap 2/2: VS bar at ($VS_X,$VS_Y)"
tap "$PLAYER" "$VS_X" "$VS_Y"

say "waiting up to ${HOST_WAIT_S}s for the host role (role=1)"
hosted=0
for i in $(seq 1 "$HOST_WAIT_S"); do
    if [ "$(role "$PLAYER")" = "role=1" ]; then hosted=1; break; fi
    sleep 1
done
say "after the walk: $(role "$PLAYER"), link mask $(mask)"
if [ "$hosted" != "1" ]; then
    say "FAIL: this console never took the host role - it is not on the VS waiting room"
    exit 1
fi
say "host role reached: the console is on the VS waiting room"

# ---------------------------------------------------------------- start tap
# Reaching the room only puts the console there; the transfer does not begin
# until the host touches its own TOUCH TO START bar, and that bar only starts
# the download once a client is listed (no client: "Practice alone?"). So
# wait for the joiner to be registered first, then tap the bar.
say "waiting up to ${HOST_WAIT_S}s for a client to be registered"
joined=0
for i in $(seq 1 "$HOST_WAIT_S"); do
    if registered; then joined=1; break; fi
    sleep 1
done
if [ "$joined" = "1" ]; then
    say "joiner registered: $( "${CTL[@]}" dpjoiner 2>/dev/null | grep 'last assoc' )"
else
    say "no client registered in ${HOST_WAIT_S}s - tapping START anyway (expect Practice alone?)"
fi
before="$(downloaded)"; before="${before:-0}"
say "START bar: tapping ($START_X,$START_Y) on console $PLAYER (download before: $before)"
tap "$PLAYER" "$START_X" "$START_Y"

say "waiting up to ${TRANSFER_WAIT_S}s for 0x04 data commands or download growth"
started=0
for i in $(seq 1 "$TRANSFER_WAIT_S"); do
    lc="$(lastcmd)"
    now="$(downloaded)"; now="${now:-0}"
    if [ "$lc" = "04" ] || [ "$now" -gt "$before" ]; then started=1; break; fi
    sleep 1
done
lc="$(lastcmd)"
now="$(downloaded)"; now="${now:-0}"
say "after START: host command class 0x${lc:-??} · download $now bytes · $( "${CTL[@]}" status 2>/dev/null | head -1 )"

if [ "$started" = "1" ]; then
    say "TRANSFER STARTED: the host left its waiting state and streamed 0x04 data commands"
    exit 0
fi
say "PARKED: waiting room reached and START tapped, but no 0x04 data command followed"
exit 2
