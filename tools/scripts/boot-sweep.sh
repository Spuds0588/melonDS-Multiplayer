#!/bin/bash
# boot-sweep.sh - boot each ROM single-player, screenshot title, quit.
# Usage: tools/scripts/boot-sweep.sh
# Writes shots to game-captures/sweep/ and a result table to stdout + logs/sweep.log
set -u
cd "$(dirname "$0")/../.."
REPO="$PWD"
OUT="$REPO/game-captures/sweep"
LOG="$REPO/logs/sweep.log"
mkdir -p "$OUT"
: > "$LOG"
CTL=(python3 "$REPO/tools/appctl.py")

declare -a ROMS=(
  "0595 - Club House Games (U)(WRG).nds"
  "Bomberman (USA).nds"
  "Diddy Kong Racing DS (USA).nds"
  "Kirby Super Star Ultra (USA).nds"
  "Mario Hoops 3 on 3 (USA).nds"
  "Mario Party DS (USA) (Rev 2).nds"
  "Meteos (USA).nds"
  "Tetris DS (USA).nds"
)

say() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"; }

for rom in "${ROMS[@]}"; do
  base="${rom%.nds}"
  tag="$(echo "$base" | tr ' ' '_' | tr -d '()')"
  say "=== BOOT: $rom ==="
  "${CTL[@]}" quit >/dev/null 2>&1; sleep 2
  MD_AUTOSTART_PLAYERS=1 MD_AUTOSTART_ROM="$REPO/Test Roms/$rom" \
    "$REPO/tools/run_app.sh" >/dev/null 2>&1 &
  sleep 60
  status="$("${CTL[@]}" status 2>/dev/null | head -1)"
  say "status: $status"
  "${CTL[@]}" shot --name "sweep_${tag}" >/dev/null 2>&1
  say "shot: sweep_${tag}"
  "${CTL[@]}" quit >/dev/null 2>&1; sleep 3
done
say "=== SWEEP DONE ==="
