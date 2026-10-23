#!/usr/bin/env bash
#
# mp-ab-run.sh - one A/B session of the reply-window measurement.
#
#   tools/scripts/mp-ab-run.sh <tag> <players> [env assignments...]
#
# HOLD_S=<seconds> keeps the session running after the route and samples the
# app's CPU use into logs/<tag>_cpu.txt, with a shot every 10s under
# logs/shots/<tag>/. That is what separates "this scene is slower" from "the
# group is waiting": a waiting group burns far less CPU per emulated second
# than a working one. The report printed at the end is
# tools/scripts/mp-session-report.py's, including the rendezvous' hold wall
# time when the run was verbose.
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
# HOLD_S: keep the session running this many seconds after the route, sampling
# the app's CPU and the consoles' pictures every 10s. A 5s health window can
# show that a scene is slower than another; only a held scene shows whether the
# group is *waiting* (low CPU) or *working* (CPU at the machine's limit), which
# is how a hold's wall cost is separated from the price of the room itself.
HOLD_S="${HOLD_S:-0}"

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

python3 tools/scripts/tetris-4p.py --players "$PLAYERS" --host 1 --start > "logs/${TAG}_route.txt" 2>&1
tail -6 "logs/${TAG}_route.txt"

if [ "$HOLD_S" -gt 0 ]; then
    # The launcher prints the pid it started; that is the process whose threads
    # do the emulating, and pgrep would happily match the supervisor instead.
    PID=$(sed -n 's/.*launched app pid \([0-9]*\).*/\1/p' "logs/${TAG}_run.out" | head -1)
    [ -n "$PID" ] || PID=$(pgrep -x melonDS-Multiplayer | head -1)
    echo "--- holding ${HOLD_S}s (app pid $PID), sampling CPU into logs/${TAG}_cpu.txt"
    python3 - "$PID" "$HOLD_S" "logs/${TAG}_cpu.txt" <<'EOF' &
import os, sys, time
def cpu(pid):
    with open(f"/proc/{pid}/stat") as f:
        parts = f.read().rsplit(")", 1)[1].split()
    return (int(parts[11]) + int(parts[12])) / os.sysconf("SC_CLK_TCK")
pid, secs, path = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
start = time.time()
prev_t, prev_c = start, cpu(pid)
with open(path, "w") as out:
    while time.time() - start < secs:
        time.sleep(2)
        now_t, now_c = time.time(), cpu(pid)
        out.write(f"t+{now_t - start:5.1f}s cpu {100.0 * (now_c - prev_c) / (now_t - prev_t):6.0f}% of one core\n")
        out.flush()
        prev_t, prev_c = now_t, now_c
EOF
    for i in $(seq 1 $((HOLD_S / 10 + 1))); do
        python3 tools/appctl.py shot --name "${TAG}_held_$i" >/dev/null 2>&1
        sleep 10
    done
    wait
fi

sleep 2
python3 tools/appctl.py quit >/dev/null 2>&1
sleep 3
echo "--- $TAG (players=$PLAYERS, $*) ---"
python3 tools/scripts/mp-session-report.py "logs/$TAG.log" 2>&1 | tail -20
[ -s "logs/${TAG}_cpu.txt" ] && { echo "--- cpu (logs/${TAG}_cpu.txt)"; tail -4 "logs/${TAG}_cpu.txt"; }
