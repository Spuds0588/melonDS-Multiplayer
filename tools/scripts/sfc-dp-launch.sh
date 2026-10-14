#!/usr/bin/env bash
#
# sfc-dp-launch.sh - confirm both players' pre-match setup into a live match.
#
# Continues where sfc-dp-host.sh ends (transfer complete, child BOOTED): after
# the boot both consoles enter match setup on their own - SHIELDS (range
# Short/Normal/Long), CONTROL TYPE (Normal/inverted), roster on the top
# screens - and neither side reaches the 3D match until its own setup is
# confirmed. The confirm is the LEFT of the three icon buttons at the bottom
# of the bottom screen, DS (64,179), measured 2026-10-14: tapping it moves
# that console to "Please wait a moment..." and, once both have confirmed,
# to the 3D dogfight (top = cockpit view, bottom = tactical map).
#
# WARNING, measured the hard way: the RIGHT icon (196,179) is Back/quit - it
# raises "This will terminate your connection", whose LEFT button proceeds
# with the disconnect and ends the session (mask 0x0, both games fatal).
# Never tap the right icon, and never tap left on that dialog.
#
# The option rows themselves only toggle the cursor (tap (128,128) flips the
# cursor between "_" and "{" with no advance); A/START/buttons do nothing on
# them (START backs out of staging to setup). Defaults (Normal/Normal) fly.
#
# Usage: run after sfc-dp-host.sh exit 0 + BOOTED (dpjoiner reads BOOTED):
#   tools/scripts/sfc-dp-launch.sh
#
# Exit 0: both consoles confirmed and both bottom screens left the setup
# (their text bands change shape); exit 1 otherwise. Steering the ships
# (D-pad UP/DOWN on either console) is the gameplay check, done by hand.
#
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

CTL_PORT="${CTL_PORT:-7373}"
CTL=(python3 tools/appctl.py --port "$CTL_PORT")
LOG="${LOG:-$PWD/logs/sfc_dp_launch.log}"
SETUP_WAIT_S="${SETUP_WAIT_S:-100}"

exec >>"$LOG" 2>&1
echo "--- sfc-dp-launch start $(date +%H:%M:%S) pid $$ port $CTL_PORT"

say() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
run() { say "ctl: $*"; "${CTL[@]}" "$@" 2>&1 | sed 's/^/    /'; }
confirm() { run touch 64 179 --player "$1" --ms 150; run untouch --player "$1"; }
bands() { "${CTL[@]}" bands --player "$1" 2>/dev/null | head -8; }

if ! "${CTL[@]}" status 2>/dev/null | grep -q "player(s)"; then
    say "FAIL: no session on port $CTL_PORT - launch the app first"
    exit 1
fi
if ! "${CTL[@]}" dpjoiner 2>/dev/null | grep -q "BOOTED"; then
    say "FAIL: download not booted yet (dpjoiner does not read BOOTED) - run sfc-dp-host.sh first"
    exit 1
fi

# The child needs a minute or more past BOOT to light its screens and reach
# setup, and in one run out of four (p65, 2026-10-14) its game never attached
# at all - three radio blips, then the "Connection error" dialog, which also
# shows text bands. A tap on any screen but the SHIELDS setup can hit Back or
# quit and end the session, so each confirm is gated on the setup signature
# (4+ strong bands: the SHIELDS/range/CONTROL rows) and the script taps
# nothing it has not identified. No signature within the wait means exit 1
# with no taps fired.
strong() { bands "$1" | awk '/^y/ && $NF >= 30' | wc -l; }
wait_setup() {
    local player="$1" waited=0
    while [ "$waited" -lt "$SETUP_WAIT_S" ]; do
        if [ "$(strong "$player")" -ge 4 ]; then return 0; fi
        sleep 10; waited=$((waited + 10))
    done
    return 1
}

say "booted; waiting up to ${SETUP_WAIT_S}s per console for the setup signature (4+ strong bands)"
if ! wait_setup 2; then
    say "FAIL: guest never reached setup (strong=$(strong 2)) - its game may not have attached; no taps fired"
    bands 2 | sed 's/^/    /'
    exit 1
fi
say "guest setup bands before confirm:"; bands 2 | sed 's/^/    /'
confirm 2
sleep 10
say "guest bands after confirm:"; bands 2 | sed 's/^/    /'

if ! wait_setup 1; then
    say "FAIL: host never reached setup (strong=$(strong 1)) - no taps fired on it"
    bands 1 | sed 's/^/    /'
    exit 1
fi
say "host setup bands before confirm:"; bands 1 | sed 's/^/    /'
confirm 1
# The guest can sit on "Please wait" for ~20 s after the host confirms before
# its own game notices both sides are ready, so check late. Setup shows 4+
# strong bands (score 30+, the SHIELDS/CONTROL rows); "Please wait" is one
# strong band (y 92..99, score ~89); the 3D match shows none - only weak
# map-graphics edges below score 30. Strong bands, not all bands, is the
# signal (measured 2026-10-14: the match bottom still carries 7 weak bands).
sleep 25
say "host bands after confirm:"; bands 1 | sed 's/^/    /'
say "guest bands now:"; bands 2 | sed 's/^/    /'
n1="$(strong 1)"
n2="$(strong 2)"
say "strong text bands now: host $n1, guest $n2 · $("${CTL[@]}" status 2>/dev/null | head -1)"
if [ "$n1" -le 1 ] && [ "$n2" -le 1 ]; then
    say "LAUNCHED: both consoles left setup for the match"
    exit 0
fi
say "PARKED: a console still shows setup bands - confirm by hand (left icon only)"
exit 1
