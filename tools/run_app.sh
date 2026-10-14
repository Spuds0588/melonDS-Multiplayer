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
#   MD_LOG            log file (default <repo>/logs/md_app.log; the previous run
#                     is kept as <log>.prev and older runs are gzipped into
#                     <dir>/archive/).
#
# Why not /tmp any more: on 2026-09-26 a reboot took /tmp with it and with it the
# log of the one run that had failed a new way (history.md v0.1.8t). Logs now
# live in the repo, where they survive both a reboot and the next run.
#   RUST_LOG          host log level (default info; "debug" is very chatty)
#   MD_LINK_VERBOSE=1 per-kind link counters in the health lines
#   MD_LINK_PROBE=1   enable packet-injecting link probes (off by default)
#   MD_REPLY_TIMEOUT_MS     per-window reply ceiling (default 34 = ~2 emulated frames)
#   MD_REPLY_BUDGET_FRAMES  emulated frames of wall clock a frame may spend on
#                           reply waits in total (default 2; negative = no cap).
#                           Both are forwarded explicitly: a knob this script does
#                           not name never reaches the app, which is how the
#                           budget override came to look like it did nothing.
#   MD_DP_REPLY_MUTE=1 the Download Play joiner associates and then answers
#                     nothing at all (nothing before the association changes).
#                     The diagnostic arm for "does the host's own phase machine
#                     advance by itself, or because of what this client sends?"
#   MD_DP_REPLY_FORCE=<hex> the Download Play joiner answers a post-RSA command
#                     it has no specific reply for with a normal reply whose
#                     third body byte is this reply type. The host's per-client
#                     mode machine dispatches on that byte (Username 07,
#                     RsaReply 08), so sweeping it names the reply the host
#                     waits for at each mode. Unset keeps the measured default:
#                     the RsaReply that carries the host from mode 14 to mode 6
#                     and starts the download.
#   MD_DP_REPLY_FORCE_DATA=<hex> the same override applied to the 04 DataPacket
#                     rounds only, so the download's later packet loop can be
#                     swept without disturbing the RsaReply that reaches mode 6.
#   MD_DP_PLAYER_BODY=1 the Download Play joiner answers one post-boot 00 poll
#                     with a UsernameReply (nickname) instead of the repeated
#                     RsaReply, testing whether a plausible guest player body
#                     advances the host's roster past Recruitment (default off).
#   MD_MP_REDELIVER=1  the reference arm of the hand-over-once guard: hand one
#                     held frame to the console's game a second time, so the
#                     guard's counter and REDELIVERED line can be seen to fire
#                     instead of only never firing (default off).
#   MD_KICK_AFTER_WINDOWS  consecutive stalled health windows before the watchdog
#                     resyncs a console's bus state (default 2; 0 = never). A
#                     console *waiting for a peer to associate* looks stalled
#                     window for window, and kicking it then discards the
#                     replies it goes on to miss - the 2026-09-25 MKDS join was
#                     broken this way, and MPH's four-console join reproduces it
#                     (history.md v0.1.9az). Set 0 while bringing several guests
#                     in at once.
#   MD_CONTROL_PORT   test-drive control channel port (default 7373; empty string
#                     turns it off). tools/appctl.py speaks it: press buttons,
#                     inject input, read the DS screens back as text, screenshots.
#
# The ROM's .sav is pinned before the app starts and compared after it exits.
# The app leaves the game in charge of its own card, so a session can write the
# save file - the harness does not (every Test Roms/*.sav survived all of
# v0.1.9ad's harness runs byte-identical) and that difference cost this pass a
# Metroid Prime - Hunters sav whose change nothing announced: the file only hits
# the disk at the session's end, so it looks untouched all run. Restoring is
# deliberately not automatic - the same launcher is used to play - but the exit
# line names the pin and the one command to put it back.
#
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/src-tauri/target/release/melonDS-Multiplayer"
LOG="${MD_LOG:-$ROOT/logs/md_app.log}"
PLAYERS="${MD_AUTOSTART_PLAYERS:-0}"
ROM="${MD_AUTOSTART_ROM:-}"
CONTROL_PORT="${MD_CONTROL_PORT-7373}"   # unset -> 7373; set to empty to disable

# Pin the ROM's save before anything can write it. Same directory and dated-
# copy naming as the passes that lost a sav the other way (game-captures/
# sav-backups), so every run leaves one and none of them overwrite each other.
SAV=""; SAV_PIN=""; SAV_MD5=""
if [ -n "$ROM" ]; then
  SAV="${ROM%.*}.sav"
  if [ -f "$SAV" ]; then
    SAV_PIN="$ROOT/game-captures/sav-backups/$(basename "$SAV").before-app-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$(dirname "$SAV_PIN")"
    cp -p "$SAV" "$SAV_PIN" 2>/dev/null || SAV_PIN=""
    [ -n "$SAV_PIN" ] && SAV_MD5=$(md5sum "$SAV" | cut -d' ' -f1)
  fi
fi

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
  # Archive the outgoing run before it becomes the single .prev slot: a session
  # that fails next week is only comparable with one from last week if last
  # week's still exists. Ten runs of history, compressed, is a few megabytes.
  logdir="$(dirname "$LOG")"
  mkdir -p "$logdir/archive"
  gzip -c "$LOG" > "$logdir/archive/$(basename "$LOG")-$(date +%Y%m%d-%H%M%S).gz" 2>/dev/null || true
  ls -1t "$logdir/archive"/*.gz 2>/dev/null | tail -n +11 | while read -r old; do rm -f "$old"; done

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
  RUST_LOG="$1" MD_AUTOSTART_PLAYERS="$2" MD_AUTOSTART_ROM="$3" \
    MD_CONTROL_PORT="$5" MD_LOG="$6" \
    MD_LINK_VERBOSE="${MD_LINK_VERBOSE:-}" \
    MD_NOJIT="${MD_NOJIT:-}" \
    MD_LINK_PROBE="${MD_LINK_PROBE:-}" \
    MD_CMD_RETRY_NOTIFY="${MD_CMD_RETRY_NOTIFY:-}" \
    MD_REPLY_TIMEOUT_MS="${MD_REPLY_TIMEOUT_MS:-}" \
    MD_REPLY_BUDGET_FRAMES="${MD_REPLY_BUDGET_FRAMES:-}" \
    MD_MP_FCS="${MD_MP_FCS:-}" MD_STOCK_MP="${MD_STOCK_MP:-}" \
    MD_RX_NOCROP="${MD_RX_NOCROP:-}" \
    MD_MP_REDELIVER="${MD_MP_REDELIVER:-}" \
    MD_REPLY_KEEP="${MD_REPLY_KEEP:-}" MD_ARM_PERSIST="${MD_ARM_PERSIST:-}" \
    MD_DP_REPLY_MUTE="${MD_DP_REPLY_MUTE:-}" \
    MD_DP_REPLY_FORCE="${MD_DP_REPLY_FORCE:-}" \
    MD_DP_REPLY_FORCE_DATA="${MD_DP_REPLY_FORCE_DATA:-}" \
    MD_DP_PLAYER_BODY="${MD_DP_PLAYER_BODY:-}" \
    MD_SAME_ROUND_REPLY="${MD_SAME_ROUND_REPLY:-}" \
    MD_KICK_AFTER_WINDOWS="${MD_KICK_AFTER_WINDOWS:-}" \
    MD_SHOTS_DIR="${MD_SHOTS_DIR:-}" "$4" &
  child=$!
  printf "[supervisor] app pid %s started\n" "$child"
  if [ -n "$8" ] && [ -n "$9" ]; then
    printf "[supervisor] sav pinned before the session: %s (md5 %s)\n" "$8" "$9"
  fi
  wait "$child"
  status=$?
  if [ "$status" -ge 128 ]; then
    printf "[supervisor] app pid %s exited with status %s (killed by signal %s)\n" \
      "$child" "$status" "$((status - 128))"
  else
    printf "[supervisor] app pid %s exited with status %s\n" "$child" "$status"
  fi
  if [ -n "$8" ] && [ -n "$9" ] && [ -f "$7" ]; then
    now=$(md5sum "$7" | cut -d" " -f1)
    if [ "$now" = "$9" ]; then
      printf "[supervisor] sav %s unchanged (md5 %s)\n" "$7" "$now"
    else
      printf "[supervisor] sav %s CHANGED (md5 %s -> %s): the game wrote its card in this session - restore it with: cp %s %s\n" \
        "$7" "$9" "$now" "$8" "$7"
    fi
  fi
' _ "${RUST_LOG:-info}" "$PLAYERS" "$ROM" "$BIN" "$CONTROL_PORT" "$LOG" "$SAV" "$SAV_PIN" "$SAV_MD5" >"$LOG" 2>&1 </dev/null &

# The app's own pid, not this script's: read it from its startup line, because
# `pgrep -f` also matches the supervisor's command line (and a kill aimed at
# that pid silently does nothing to the emulator).
sleep 3
app_pid=$(grep -m1 -oE "starting \(pid [0-9]+\)" "$LOG" 2>/dev/null | grep -oE "[0-9]+")
echo "launched${app_pid:+ app pid $app_pid}"
echo "log:    $LOG   (previous run: $LOG.prev, older runs: $(dirname "$LOG")/archive/)"
echo "crash:  /tmp/md_crash_${app_pid:-<pid>}.txt"
echo "exit:   the log's last line records the exit status/signal when the app ends"
if [ "$PLAYERS" != "0" ]; then
  echo "autostart: $PLAYERS player(s), rom $ROM"
fi
if [ -n "$CONTROL_PORT" ]; then
  echo "control:  tools/appctl.py --port $CONTROL_PORT (see | status | press a --player 1)"
fi
if [ -n "$SAV_PIN" ]; then
  echo "sav:      pinned $SAV (md5 $SAV_MD5) to $SAV_PIN; the log's last lines say whether the app wrote it"
fi
