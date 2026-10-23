#!/usr/bin/env bash
#
# mp-ab-run.sh - one A/B session of the reply-window measurement.
#
#   tools/scripts/mp-ab-run.sh <tag> <players> [env assignments...]
#
# Launches the app with a fresh log under logs/<tag>.log and shots under
# logs/shots/<tag>/, drives the consoles through tools/scripts/tetris-4p.py
# into Tetris DS's local-wireless room, then quits the app and prints the
# reply-window summary for that log. The env assignments are passed through to
# the app (MD_REPLY_EXTEND_FRAMES=0 etc.), which is the point: same binary,
# same route, one knob apart.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

TAG="$1"; PLAYERS="$2"; shift 2
ROM="$PWD/Test Roms/Tetris DS (USA).nds"
SAV="$PWD/Test Roms/Tetris DS (USA).sav"

python3 tools/appctl.py quit >/dev/null 2>&1
sleep 2

env "$@" \
    MD_AUTOSTART_PLAYERS="$PLAYERS" \
    MD_AUTOSTART_ROM="$ROM" \
    MD_LINK_VERBOSE=1 \
    MD_LOG="$PWD/logs/$TAG.log" \
    MD_SHOTS_DIR="$PWD/logs/shots/$TAG" \
    nohup tools/run_app.sh > "logs/${TAG}_run.out" 2>&1 &

for _ in $(seq 1 40); do
    if python3 tools/appctl.py ping >/dev/null 2>&1; then break; fi
    sleep 1
done

python3 tools/scripts/tetris-4p.py --players "$PLAYERS" --host 1 --start 2>&1 | tail -6

sleep 2
python3 tools/appctl.py quit >/dev/null 2>&1
sleep 3
echo "--- $TAG (players=$PLAYERS, $*) ---"
python3 tools/scripts/mp-window-latency.py "logs/$TAG.log" 2>&1 | tail -8
