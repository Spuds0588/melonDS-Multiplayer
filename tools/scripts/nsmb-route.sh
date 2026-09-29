#!/bin/sh
# The New Super Mario Bros. route that reaches the game's own local-wireless
# play: three one-shot touch taps walk both consoles into it, and a repeating
# tap plus a repeating A keep them there until --stop-input ends all input at
# frame 12000, so what runs after that is the session on its own.
#
# This script is the ONLY copy of those coordinates and frame numbers. They used
# to live here, in BUILD.md's "Real game session" section and in
# nsmb-drop-rate.sh, and the copies drifted: the route BUILD.md documented was
# written against a *fresh* save's boot flow and reaches no lobby at all on this
# tree's save (0 slots, 0 packets). A route is one-shot taps at the exact frame a
# given boot reaches a given screen, so any change to the save or the tree has to
# be re-timed here once, not in three places. Next to that, `FRAMES`, the
# `@N` player suffixes and the emulated offsets of the digest lines are the
# numbers a re-timing has to move together.
#
#   tools/scripts/nsmb-route.sh                 # one session, 14000 frames
#   tools/scripts/nsmb-route.sh --shot-dir game-captures/run --shot-every 200
#   FRAMES=8000 tools/scripts/nsmb-route.sh     # shorter run
#   BIN=... ROM=... SAV=... FRAMES=... tools/scripts/nsmb-route.sh
#
# The save is restored first (the backup is what the route was timed against,
# and running a session mutates the save), and everything else is passed
# straight through to the harness - so every md_game_session option still
# applies. Expect `slots on the bus: 0x003` and a last command at about +234.00s
# on a 14000-frame run: those two, not the absence of an error, are what say the
# route landed.
set -eu

BIN=${BIN:-./build-tauri/md_game_session}
ROM=${ROM:-Test Roms/New Super Mario Bros. (USA).nds}
SAV=${SAV:-Test Roms/New Super Mario Bros. (USA).sav}
BACKUP=${BACKUP:-/tmp/nsmb_sav_backup.bin}
FRAMES=${FRAMES:-14000}

if [ -f "$BACKUP" ]; then
    cp "$BACKUP" "$SAV"
fi

exec "$BIN" "$ROM" --players 2 --frames "$FRAMES" --all-players \
    --touch 612:128:95 --touch 1292:128:72 \
    --touch 2012:127:119@1 --touch 2012:127:160@2 \
    --touchtap 128:171:2600:160 --autotap A:2700:60 \
    --stop-input 12000 "$@"
