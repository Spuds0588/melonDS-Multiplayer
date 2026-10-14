#!/usr/bin/env bash
#
# sm64-dp-launch.sh - drive both Super Mario 64 DS consoles from the booted
# download through character/stage setup toward the VS battle.
#
# Continues where sm64-dp-host.sh ends (transfer complete, child BOOTED).
# The menu geometry below was measured off framebuffer captures 2026-10-14
# (the game's font defeats the ASCII readers, so positions are DS pixels,
# verified by before/after shot digests, never guessed):
#
#   character select   four panels y40..96: P1 picks panel 1 (34,68),
#                      P2 picks panel 2 (98,68). Each pick dims the other
#                      panels on BOTH consoles (shared lobby state).
#   stage screen       3D preview + headline. D-pad cycles the stage; A,
#                      START and headline taps do nothing. The screen needs
#                      ~60 s to settle before it accepts the guest's tap.
#   hub                reached by the GUEST's headline tap (128,60 on
#                      player 2): P1 shows two labeled boxes, P2 a waiting
#                      message. P1 box1 (128,72) goes back to character
#                      select; P1 box2 (128,120) goes forward.
#   2-button confirm   LEFT (76,168) confirms, RIGHT (180,168) backs out
#                      to the hub (digest-proven both ways).
#   converged screen   both consoles show the box+6-dots screen. Twice the
#                      host has aborted here within seconds (radio off, no
#                      deauth, mask 0x0) with the guest never confirmed, so
#                      the script taps the guest's box (128,92 on player 2)
#                      immediately after the host's LEFT - that is the one
#                      open variable (see CONFIRM_GUEST, default on).
#
# Usage: run after sm64-dp-host.sh exit 0 + BOOTED (dpjoiner reads BOOTED):
#   tools/scripts/sm64-dp-launch.sh
#
# Exit 0: the link is still up (mask nonzero) 15 s after the confirms.
# Exit 1: otherwise (parked or dead - the log says which step stopped).
#
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

CTL_PORT="${CTL_PORT:-7373}"
CTL=(python3 tools/appctl.py --port "$CTL_PORT")
LOG="${LOG:-$PWD/logs/sm64_dp_launch.log}"
SETTLE_S="${SETTLE_S:-70}"
CONFIRM_GUEST="${CONFIRM_GUEST:-1}"

exec >>"$LOG" 2>&1
echo "--- sm64-dp-launch start $(date +%H:%M:%S) pid $$ port $CTL_PORT"

say()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
run()  { say "ctl: $*"; "${CTL[@]}" "$@" 2>&1 | sed 's/^/    /'; }
tap()  { run touch "$2" "$3" --player "$1" --ms 150; run untouch --player "$1"; }
mask() { "${CTL[@]}" status 2>/dev/null | sed -n 's/.*link mask \(0x[0-9a-fA-F]*\).*/\1/p' | head -1; }

if ! "${CTL[@]}" status 2>/dev/null | grep -q "player(s)"; then
    say "FAIL: no session on port $CTL_PORT - launch the app first"
    exit 1
fi
if ! "${CTL[@]}" dpjoiner 2>/dev/null | grep -q "BOOTED"; then
    say "FAIL: download not booted yet (dpjoiner does not read BOOTED) - run sm64-dp-host.sh first"
    exit 1
fi

# The child needs a minute or more past BOOT to light its screens and attach
# (first run: guest rendering ~2 min post-boot, live MP exchange both ways).
# There is no text signature to gate on (font defeats the readers), so this
# is a fixed wait, not a readiness check.
say "booted; waiting 90 s for the guest's game to attach"
sleep 90
say "state: $( "${CTL[@]}" status 2>/dev/null | head -1 )"

say "character picks: P1 panel 1 (34,68), P2 panel 2 (98,68)"
tap 1 34 68
sleep 5
tap 2 98 68
sleep 5

say "stage screen; settling ${SETTLE_S} s before the guest's confirm tap"
sleep "$SETTLE_S"
say "guest headline tap (128,60 on player 2) to the hub"
tapp() { tap 2 128 60; }
tapp
sleep 6

say "hub: P1 box2 (128,120) forward to the 2-button confirm"
tap 1 128 120
sleep 8

say "confirm: P1 LEFT (76,168)"
tap 1 76 168
if [ "$CONFIRM_GUEST" = "1" ]; then
    say "guest confirm: P2 box (128,92) immediately (the open variable)"
    sleep 2
    tap 2 128 92
fi

sleep 15
final_mask="$(mask)"
say "15 s after the confirms: link mask ${final_mask:-?} · $( "${CTL[@]}" status 2>/dev/null | head -1 )"
if [ "${final_mask:-0x0}" != "0x0" ] && [ -n "${final_mask:-}" ]; then
    say "LAUNCHED: link still up after the confirms"
    exit 0
fi
say "PARKED: link down after the confirms - see the step log above"
exit 1
