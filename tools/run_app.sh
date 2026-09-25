#!/usr/bin/env bash
#
# Launch the desktop app for manual testing, with logging that can be read
# afterwards. Written after losing three sessions to "the window vanished and
# the log just stops": every run now gets a fresh log, a crash-report file, and
# a known pid.
#
#   tools/run_app.sh                                  # empty app, pick a ROM in the UI
#   MD_AUTOSTART_PLAYERS=2 MD_AUTOSTART_ROM="/abs/path/game.nds" tools/run_app.sh
#
# Environment:
#   MD_LOG            log file (default /tmp/md_app.log; the previous run is
#                     kept as <log>.prev)
#   RUST_LOG          host log level (default info; "debug" is very chatty)
#   MD_LINK_VERBOSE=1 per-kind link counters in the health lines
#   MD_LINK_PROBE=1   enable packet-injecting link probes (off by default)
#
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/src-tauri/target/release/melonDS-Multiplayer"
LOG="${MD_LOG:-/tmp/md_app.log}"
PLAYERS="${MD_AUTOSTART_PLAYERS:-0}"
ROM="${MD_AUTOSTART_ROM:-}"

if [ ! -x "$BIN" ]; then
  echo "app binary not built: $BIN" >&2
  echo "build it with: cargo build --release --manifest-path src-tauri/Cargo.toml" >&2
  exit 1
fi

# Stop a previous instance. Matching on the binary path is enough and cannot
# match this script (whose command line mentions neither the path nor -f
# patterns that include it) - an earlier attempt with `pkill -f` in a one-liner
# killed the launching shell itself.
while read -r pid; do
  [ -z "$pid" ] && continue
  kill "$pid" 2>/dev/null || true
done < <(pgrep -f "$BIN" 2>/dev/null || true)
sleep 1

if [ -f "$LOG" ]; then
  mv -f "$LOG" "$LOG.prev"
fi

# Run the app under a one-line supervisor whose only job is to record the exit
# status. That line is the one thing an in-process handler cannot give us: the
# ARM JIT owns SIGSEGV while a session runs, and it chains unhandled faults to
# whichever handler was current when it registered (Rust's std, installed before
# main), not to ours - so a genuine SIGSEGV can end the process with no report.
# "app exited with status 139 (SIGSEGV)" vs "status 0" vs no line at all
# separates a segfault, a clean close, and a still-running app.
setsid nohup bash -c '
  RUST_LOG="$1" MD_AUTOSTART_PLAYERS="$2" MD_AUTOSTART_ROM="$3" "$4" &
  child=$!
  printf "[supervisor] app pid %s started\n" "$child"
  wait "$child"
  status=$?
  if [ "$status" -ge 128 ]; then
    printf "[supervisor] app pid %s exited with status %s (killed by signal %s)\n" \
      "$child" "$status" "$((status - 128))"
  else
    printf "[supervisor] app pid %s exited with status %s\n" "$child" "$status"
  fi
' _ "${RUST_LOG:-info}" "$PLAYERS" "$ROM" "$BIN" >"$LOG" 2>&1 </dev/null &

# The app's own pid, not this script's: read it from its startup line, because
# `pgrep -f` also matches the supervisor's command line (and a kill aimed at
# that pid silently does nothing to the emulator).
sleep 3
app_pid=$(grep -m1 -oE "starting \(pid [0-9]+\)" "$LOG" 2>/dev/null | grep -oE "[0-9]+")
echo "launched${app_pid:+ app pid $app_pid}"
echo "log:    $LOG   (previous run: $LOG.prev)"
echo "crash:  /tmp/md_crash_${app_pid:-<pid>}.txt"
echo "exit:   the log's last line records the exit status/signal when the app ends"
if [ "$PLAYERS" != "0" ]; then
  echo "autostart: $PLAYERS player(s), rom $ROM"
fi
