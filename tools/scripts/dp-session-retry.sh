#!/usr/bin/env bash
#
# dp-session-retry.sh - beat the Download Play attach lottery by retrying the
# session instead of waiting out a dead one.
#
# The guest attach is a coin flip that stays lost across titles (SFC 4/7,
# SM64 2/4): when it loses, the host stops transmitting ~10-30 s post-boot
# with no deauth and both games error minutes later. The outcome is readable
# from the app log within ~25 s of BOOT (dp_attach_gate.py: 0 attached,
# 1 dead, 2 too early), so this driver relaunches until the gate passes
# instead of driving menus on a dead session.
#
# Required env: ROM (the cartridge path).
# Optional env:
#   PLAYERS (default 2), HOST_SCRIPT (default tools/scripts/sfc-dp-host.sh),
#   NEXT_SCRIPT (run after the gate passes, e.g. the title's launch script;
#     empty leaves the attached session up and exits 0),
#   MAX_TRIES (default 3), TRY_LOG_STEM (default logs/dp_retry; per-try logs
#     are <stem>_t<N>_app.log / <stem>_t<N>_host.log),
#   GATE_POLLS (default 12, 10 s apart), CTL_PORT (default 7373).
# MD_LINK_VERBOSE=1 is forced on: the gate reads verbose digest lines.
#
# Exit 0: the gate passed (and NEXT_SCRIPT, if set, then ran - its code is
# returned). Exit 1: every try lost the lottery, or no try reached BOOT.
#
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

ROM="${ROM:-}"
PLAYERS="${PLAYERS:-2}"
HOST_SCRIPT="${HOST_SCRIPT:-tools/scripts/sfc-dp-host.sh}"
NEXT_SCRIPT="${NEXT_SCRIPT:-}"
MAX_TRIES="${MAX_TRIES:-3}"
TRY_LOG_STEM="${TRY_LOG_STEM:-$PWD/logs/dp_retry}"
GATE_POLLS="${GATE_POLLS:-12}"
CTL_PORT="${CTL_PORT:-7373}"
export MD_LINK_VERBOSE=1

if [ -z "$ROM" ]; then echo "FAIL: set ROM=/path/to/game.nds" >&2; exit 1; fi
if [ ! -x "$HOST_SCRIPT" ]; then echo "FAIL: HOST_SCRIPT not executable: $HOST_SCRIPT" >&2; exit 1; fi

quit_app() { python3 tools/appctl.py --port "$CTL_PORT" quit >/dev/null 2>&1; sleep 3; }

try=0
while [ "$try" -lt "$MAX_TRIES" ]; do
    try=$((try + 1))
    APP_LOG="${TRY_LOG_STEM}_t${try}_app.log"
    HOST_LOG="${TRY_LOG_STEM}_t${try}_host.log"
    echo "=== try $try/$MAX_TRIES ($(date +%H:%M:%S)): rom $(basename "$ROM") ==="
    MD_AUTOSTART_PLAYERS="$PLAYERS" MD_AUTOSTART_ROM="$ROM" MD_LOG="$APP_LOG" tools/run_app.sh
    sleep 20
    if ! python3 tools/appctl.py --port "$CTL_PORT" status 2>/dev/null | grep -q "player(s)"; then
        echo "try $try: no session came up - relaunching"
        quit_app
        continue
    fi
    LOG="$HOST_LOG" APP_LOG="$APP_LOG" CTL_PORT="$CTL_PORT" "$HOST_SCRIPT"
    host_rc=$?
    if [ "$host_rc" != "0" ]; then
        echo "try $try: host route failed (exit $host_rc) - relaunching"
        quit_app
        continue
    fi
    echo "try $try: host route green, polling the attach gate"
    gate_rc=2
    for i in $(seq 1 "$GATE_POLLS"); do
        sleep 10
        gate_out="$(python3 tools/scripts/dp_attach_gate.py "$APP_LOG" 2>&1)"
        gate_rc=$?
        echo "try $try gate poll $i: $gate_out"
        if [ "$gate_rc" != "2" ]; then break; fi
    done
    if [ "$gate_rc" = "0" ]; then
        echo "try $try: ATTACHED - session is alive"
        if [ -n "$NEXT_SCRIPT" ]; then
            LOG="${TRY_LOG_STEM}_t${try}_launch.log" APP_LOG="$APP_LOG" CTL_PORT="$CTL_PORT" "$NEXT_SCRIPT"
            exit $?
        fi
        exit 0
    fi
    echo "try $try: gate refused (rc=$gate_rc) - quitting and relaunching"
    quit_app
done
echo "FAIL: all $MAX_TRIES tries lost the lottery"
exit 1
