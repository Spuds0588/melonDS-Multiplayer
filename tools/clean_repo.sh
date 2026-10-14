#!/usr/bin/env bash
#
# Reclaim disk space from regenerable artifacts. Safe by design: everything it
# deletes is rebuildable, and evidence referenced from the docs is kept.
#
#   tools/clean_repo.sh              # dry run: list what would be deleted
#   tools/clean_repo.sh --apply      # actually delete
#
# What it cleans, and why each thing is safe:
#   src-tauri/target/   Rust/Tauri build output. Rebuilt by cargo on next build.
#   build-tauri/        CMake build tree for the capture/smoke-test tools.
#                       Rebuilt per BUILD.md.
#   __pycache__/        Python bytecode caches.
#   logs/               Run logs from tools/run_app.sh (gitignored). Kept for
#                       LOG_DAYS days (default 14) - evidence for recent
#                       sessions, garbage after the session is written up in
#                       history.md.
#   game-captures/      Frame captures + sav backups (gitignored, may contain
#                       game imagery). Capture sets not modified in KEEP_DAYS
#                       days (default 14) are deleted, EXCEPT sets referenced
#                       from the docs (history.md / to-do.md / compatibility.md
#                       / BUILD.md name them as evidence). sav-backups keeps
#                       only the newest backup per base .sav name; never
#                       touches anything in "Test Roms/".
#
# Never touched: git-tracked files, "Test Roms/", tools/, src/, docs.
#
set -u

APPLY=0
[ "${1:-}" = "--apply" ] && APPLY=1

LOG_DAYS="${LOG_DAYS:-14}"
KEEP_DAYS="${KEEP_DAYS:-14}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

deleted=0
freed=0

note() {  # note <size> <path...>
  freed=$((freed + $1))
  echo "  $2"
  deleted=$((deleted + 1))
}

rm_path() {  # rm_path <path...>
  local size
  size=$(du -sk "$@" 2>/dev/null | awk '{s+=$1} END {print s+0}')
  if [ "$APPLY" = 1 ]; then rm -rf "$@"; fi
  note "$size" "$*"
}

echo "melonDS Multiplayer repo cleanup (LOG_DAYS=$LOG_DAYS KEEP_DAYS=$KEEP_DAYS$(if [ "$APPLY" = 1 ]; then echo ", APPLYING"; fi))"
[ "$APPLY" = 0 ] && echo "(dry run - pass --apply to delete)"

# --- regenerable build output -------------------------------------------
for p in src-tauri/target build-tauri; do
  [ -e "$p" ] && rm_path "$p"
done
while IFS= read -r -d '' pyc; do rm_path "$pyc"; done \
  < <(find . -type d -name __pycache__ -not -path './.git/*' -print0 2>/dev/null)

# --- old run logs --------------------------------------------------------
if [ -d logs ]; then
  echo "logs/ older than $LOG_DAYS days:"
  while IFS= read -r -d '' f; do rm_path "$f"; done \
    < <(find logs -maxdepth 1 -type f -mtime +"$LOG_DAYS" -print0)
  # shots/ follow the same age rule
  while IFS= read -r -d '' f; do rm_path "$f"; done \
    < <(find logs/shots -type f -mtime +"$LOG_DAYS" -print0 2>/dev/null)
fi

# --- superseded game captures -------------------------------------------
if [ -d game-captures ]; then
  # Sets the docs cite as evidence are never deleted, however old.
  dockeep=$(grep -rhoE 'game-captures/[A-Za-z0-9._-]+' \
    history.md to-do.md compatibility.md BUILD.md README.md 2>/dev/null |
    sed 's|^game-captures/||' | sort -u)

  echo "game-captures/ sets older than $KEEP_DAYS days (doc-referenced sets kept):"
  while IFS= read -r -d '' d; do
    name=$(basename "$d")
    if printf '%s\n' "$dockeep" | grep -qxF "$name"; then continue; fi
    rm_path "$d"
  done < <(find game-captures -mindepth 1 -maxdepth 1 -type d -not -name sav-backups \
             -mtime +"$KEEP_DAYS" -print0)

  # sav-backups: keep only the newest backup per base .sav name.
  python3 - "$APPLY" <<'PYEOF'
import os, re, sys
d = 'game-captures/sav-backups'
if not os.path.isdir(d):
    sys.exit(0)
apply = sys.argv[1] == '1'
groups = {}
for f in os.listdir(d):
    m = re.match(r'(.*\.sav)\.(?:before|after).*?(\d{8}-\d{6})$', f) \
        or re.match(r'(.*\.sav)\.(\d{8}-\d{6})$', f)
    if m:
        groups.setdefault(m.group(1), []).append((m.group(2), f))
removed = 0
for base, lst in sorted(groups.items()):
    lst.sort()
    for _, f in lst[:-1]:
        if apply:
            os.remove(os.path.join(d, f))
        removed += 1
print(f"sav-backups: {'removed' if apply else 'would remove'} {removed} old backups, "
      f"kept newest of each ({len(groups)} games)")
PYEOF
fi

echo
if [ "$APPLY" = 1 ]; then
  echo "done: $deleted paths deleted, ~$((freed / 1024)) MB freed"
else
  echo "dry run: $deleted paths, ~$((freed / 1024)) MB would be freed (pass --apply)"
fi
