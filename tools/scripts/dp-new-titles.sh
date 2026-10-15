#!/bin/bash
# dp-new-titles.sh - Download Play on the newly added shelf titles.
#
# Why: Milestone 4's single-card path was proven on Star Fox Command and
# Super Mario 64 DS. The shelf grew (Tetris DS, Clubhouse Games, Bomberman,
# Diddy Kong Racing, Kirby Super Star Ultra, Mario Hoops, Mario Party, Meteos)
# and none of them had ever been booted in the app, let alone linked. This
# driver answers two questions per title, in order:
#
#   1. Does it boot and render in the app at all? (a title that cannot boot
#      cannot be tested, and a black screen is a result, not a nuisance)
#   2. What does its own multiplayer menu look like, and does the app's
#      synthesized Download Play joiner see a host beacon when the title is
#      driven into its single-card host screen?
#
# It never guesses a route. The title screen and the first menu page are
# reached with one geometry-measured tap each (the same trick sfc-dp-host.sh
# uses: on every title measured so far the title screen accepts a touch
# anywhere, and the first menu item sits low enough that one coordinate lands
# on it), and everything after that is read from the emulator's own log, not
# from a screen reader.
#
# The measurement it produces is the joiner's own `[dp]` lines: a beacon heard
# means the title's host screen beacons and the joiner is armed against it; no
# beacon means either the route did not reach the host screen or the title's
# single-card mode is not behind this menu, and both are recorded as such.
#
# Usage:
#   tools/scripts/dp-new-titles.sh                 # every title below
#   tools/scripts/dp-new-titles.sh "Tetris DS (USA).nds"
#
# Output: logs/dpnew_<tag>.log per title (the app's own log) and one summary
# line per title on stdout + logs/dp-new-titles.log.
set -u
cd "$(dirname "$0")/../.."
REPO="$PWD"
SUMMARY="$REPO/logs/dp-new-titles.log"
SETTLE_S="${SETTLE_S:-75}"

CTL=(python3 "$REPO/tools/appctl.py")
say() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$SUMMARY"; }

# Title screen: one tap anywhere near the middle of the bottom screen.
TITLE_TAP_X="${TITLE_TAP_X:-128}"
TITLE_TAP_Y="${TITLE_TAP_Y:-96}"
# First menu item on the bottom screen, measured for the SFC layout and used
# here as the *default* only: a title whose menu is laid out differently simply
# records "no beacon", which is the honest result for an unmapped route.
MENU_TAP_X="${MENU_TAP_X:-128}"
MENU_TAP_Y="${MENU_TAP_Y:-120}"

if [ "$#" -gt 0 ]; then
    TITLES=("$@")
else
    TITLES=(
        "Tetris DS (USA).nds"
        "0595 - Club House Games (U)(WRG).nds"
        "Bomberman (USA).nds"
        "Diddy Kong Racing DS (USA).nds"
        "Kirby Super Star Ultra (USA).nds"
        "Mario Hoops 3 on 3 (USA).nds"
        "Mario Party DS (USA) (Rev 2).nds"
        "Meteos (USA).nds"
    )
fi

: >> "$SUMMARY"

for rom in "${TITLES[@]}"; do
    tag="$(echo "${rom%.nds}" | tr ' ' '_' | tr -d '()')"
    applog="$REPO/logs/dpnew_${tag}.log"
    say "=== $rom ==="

    "${CTL[@]}" quit >/dev/null 2>&1
    sleep 3

    MD_LOG="$applog" MD_AUTOSTART_PLAYERS=2 \
        MD_AUTOSTART_ROM="$REPO/Test Roms/$rom" \
        "$REPO/tools/run_app.sh" >/dev/null 2>&1
    sleep "$SETTLE_S"

    status="$("${CTL[@]}" status 2>/dev/null | head -1)"
    say "boot: $status"
    # The profile line is the app's own statement of whether this title is
    # recognized as a Download Play title and whether the joiner is armed.
    profile="$(grep -m1 "loaded game:" "$applog" 2>/dev/null | sed 's/.*loaded game: //')"
    say "profile: ${profile:-<none>}"

    # Drive the title screen and the first menu page, then watch the link.
    "${CTL[@]}" touch "$TITLE_TAP_X" "$TITLE_TAP_Y" --player 1 >/dev/null 2>&1
    sleep 4
    "${CTL[@]}" touch "$MENU_TAP_X" "$MENU_TAP_Y" --player 1 >/dev/null 2>&1
    sleep 4
    "${CTL[@]}" touch "$MENU_TAP_X" "$MENU_TAP_Y" --player 1 >/dev/null 2>&1
    sleep 8

    beacons="$(grep -c "\[dp\] hearing a download-play beacon" "$applog" 2>/dev/null || true)"
    armed="$(grep -c "download-play joiner: console 2 is the synthesized client, armed" "$applog" 2>/dev/null || true)"
    say "joiner armed: ${armed:-0}; beacons heard: ${beacons:-0}"

    if [ "${beacons:-0}" -gt 0 ]; then
        grep -m1 "\[dp\] hearing a download-play beacon" "$applog" | cut -c1-220 | tee -a "$SUMMARY"
    fi

    "${CTL[@]}" shot --name "dpnew_${tag}" >/dev/null 2>&1
    "${CTL[@]}" quit >/dev/null 2>&1
    sleep 3
done

say "=== DP NEW-TITLE PASS DONE ==="
