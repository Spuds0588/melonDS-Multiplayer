#!/usr/bin/env bash
#
# tetris-4p.sh - drive four Tetris DS consoles into one local-wireless room.
#
# Why this exists: the 4-player question for this title is not the link, it is
# the route. Tetris DS's multiplayer lives behind touch bars whose positions
# were measured off the game's own framebuffer (INVITE y~48, JOIN y~96, BACK
# y~144, and the main menu's MULTIPLAYER bar at y~60), and the host and the
# joiners take different branches of the same page. This script walks all four
# consoles through that route in one go and prints the evidence after every
# step - the app's own link mask and each console's wifi role, plus a screenshot
# per step - so a route that lands on the wrong page is visible instead of being
# driven further.
#
# It decides nothing from screenshots: every step is `appctl.py` input, and the
# oracle is the emulator's own state (`status`'s link mask, `wifiio`'s role).
#
# What the exit code does **not** say: that four players are in the room. PASS is
# a bus-level verdict (every console's radio attached). The room itself does take
# three guests - the 2026-10-16 pass read each of them listed on the host's own
# room screen - but the party still does not start: the host's start press hands
# its cart guests a Download Play `04` DataPacket their own game cannot use, and
# they deauth on it (compatibility.md, to-do.md). So a driver that wants a
# *player count*, or wants to know whether the party started, has to read the
# consoles' own screens (`shot` + the shots this script writes per step), not
# this exit code.
#
# Usage:
#   tools/scripts/tetris-4p.sh            # host p1, joiners p2 p3 p4
#   HOST=1 JOINERS="2 3" tools/scripts/tetris-4p.sh
#
# Exit codes: 0 = host role seen and every joiner's radio on the bus,
#             1 = the host never took the room, 2 = the host is up but a
#             joiner never reached the bus.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

CTL=(python3 tools/appctl.py)
HOST="${HOST:-1}"
JOINERS="${JOINERS:-2 3 4}"
TAG="${TAG:-t4p}"

say() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
mask() { "${CTL[@]}" status 2>/dev/null | sed -n 's/.*link mask \(0x[0-9a-fA-F]*\).*/\1/p' | head -1; }
role() { "${CTL[@]}" wifiio --player "$1" 2>/dev/null | sed -n 's/.*role=\([0-9]*\).*/\1/p' | head -1; }
press() { "${CTL[@]}" press "$2" --player "$1" --ms "${3:-200}" >/dev/null 2>&1; }
tap() { "${CTL[@]}" touch "$2" "$3" --player "$1" --ms 150 >/dev/null 2>&1; "${CTL[@]}" untouch --player "$1" >/dev/null 2>&1; }
shot() { "${CTL[@]}" shot --name "$1" >/dev/null 2>&1; }

step() {  # step <tag> <description>
    echo
    say "--- $1: $2"
    say "host p$HOST role=$(role "$HOST")  mask=$(mask)"
    for j in $JOINERS; do say "joiner p$j role=$(role "$j")"; done
    shot "${TAG}_$1"
}

step 00_start "session up, before any input"

# ---------------------------------------------------------------- title screen
# The logo screen accepts START (measured: the digest moves on the first press).
for p in "$HOST" $JOINERS; do press "$p" start 600; sleep 1; done
sleep 3
step 01_title "START on every console"

# ---------------------------------------------------------------- main menu
# One DOWN moves the menu cursor from its first entry onto MULTIPLAYER
# (measured on a fresh boot: DOWN then A lands on the INVITE/JOIN page).
for p in "$HOST" $JOINERS; do press "$p" down; sleep 1; done
sleep 2
for p in "$HOST" $JOINERS; do press "$p" a; sleep 1; done
sleep 3
step 02_multipage "DOWN + A on every console (MULTI PLAYER page)"

# ---------------------------------------------------------------- host: invite
press "$HOST" a 300
sleep 4
step 03_invite "host selected INVITE"
press "$HOST" a 300
sleep 5
step 04_mode "host selected the first mode (STANDARD)"

host_role="$(role "$HOST")"
if [ "$host_role" != "1" ]; then
    say "FAIL: the host never took the room (role=$host_role)"
    exit 1
fi
say "host room up: role=1"

# ------------------------------------------------------------- joiners: JOIN
# The join is three inputs, measured on the console itself: the JOIN bar sits at
# y~96 on the MULTI PLAYER page (INVITE is y~48, BACK is y~144), tapping it
# selects it, and the first A opens the room list - the second A is what actually
# puts this console's game into MP mode and enters the room the host is running
# (`wifiio` goes from `role=0 USCOUNT=0 zone gen=0` to `role=2` with frames being
# handed over). Without the A presses the console sits on the page with its radio
# silent - the mistake that made an earlier pass read this page as "unreachable".
join_one() {
    local j="$1"
    tap "$j" 128 96
    sleep 5
    press "$j" a 250
    sleep 7
    press "$j" a 250
    sleep 8
    say "p$j joined: role=$(role "$j")  mask=$(mask)"
    shot "${TAG}_05_join_p$j"
}

for j in $JOINERS; do join_one "$j"; done

step 06_joined "all joiners asked to JOIN"

# Watch, do not just sample once: this title's guests are lost seconds after a
# later console associates, which a single snapshot cannot see.
WATCH_S="${WATCH_S:-60}"
say "watching ${WATCH_S}s for the room to hold"
for i in $(seq 1 "$WATCH_S"); do
    sleep 1
    if [ $((i % 10)) = 0 ]; then
        say "t+${i}s mask=$(mask) roles: $(for p in "$HOST" $JOINERS; do printf 'p%s=%s ' "$p" "$(role "$p")"; done)"
    fi
done
shot "${TAG}_07_watched"
step 08_end "end of the watch"

ok=1
for j in $JOINERS; do
    bit=$(( ( $(printf '%d' "$(mask)") >> (j - 1) ) & 1 ))
    [ "$bit" = "1" ] || ok=0
done
if [ "$ok" = "1" ]; then
    say "PASS: host room up and every joiner's radio is on the bus ($(mask))"
    exit 0
fi
say "PARTIAL: host room up, but not every joiner reached the bus (mask $(mask))"
exit 2
