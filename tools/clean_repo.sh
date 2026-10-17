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
#                       history.md. Deleted by age, EXCEPT files the docs cite
#                       as evidence - by path, bare name, or a family glob like
#                       `logs/v9c*` - which are kept however old, the same rule
#                       the capture sets below get. Without it a log family a
#                       doc quotes would be deleted on every run.
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
  # Files the docs cite as evidence are never deleted, however old. They are
  # named there three ways, so all three are matched: by path
  # (`logs/shots/x.png`), by bare name (`x.png`), and as a glob over a whole
  # family (`logs/v9c*`, `logs/shots/tetris_*.png`), which is the form the docs
  # use most. Trailing punctuation from the citing sentence is trimmed so it
  # cannot make a cited file miss the guard.
  logkeep=$(
    { grep -rhoE 'logs/[A-Za-z0-9._/-]+' \
      history.md to-do.md compatibility.md BUILD.md README.md 2>/dev/null |
      sed -e 's|^logs/||' -e 's|[.,;:]+$||';
      grep -rhoE '[A-Za-z0-9._-]+\.(log|txt|png|jpg|gif|gz|webm|wav)' \
      history.md to-do.md compatibility.md BUILD.md README.md 2>/dev/null
    } | sort -u
  )
  # The same references with globs kept intact, for the family match below.
  logglobs=$(grep -rhoE 'logs/[A-Za-z0-9._*/-]+' \
    history.md to-do.md compatibility.md BUILD.md README.md 2>/dev/null |
    sed -e 's|^logs/||' -e 's|[.,;:]+$||' | sort -u)

  # A doc shorthand range (`logs/v9d2..v9d5`) cites a family without naming its
  # files, so no name or glob can match it. Turn each range into the stems it
  # spans and keep the logs under them; a form the expansion cannot read still
  # contributes both of its endpoints as stems, which keeps more, never less.
  logstems=$(
    printf '%s\n' "$logglobs" | grep -E '\.\.' | while IFS= read -r pat; do
      a=${pat%%..*}; b=${pat##*..}
      pa=${a%%[0-9]*}; na=${a#"$pa"}; pb=${b%%[0-9]*}; nb=${b#"$pb"}
      if [ "$pa" = "$pb" ] && [ -n "$na" ] && [ -n "$nb" ] &&
         [ "$na" -le "$nb" ] 2>/dev/null; then
        for ((i = 10#$na; i <= 10#$nb; i++)); do printf '%s%s\n' "$pa" "$i"; done
      else
        printf '%s\n%s\n' "$a" "$b"
      fi
    done | sort -u
  )

  # log_cited <path> - is it named in the docs as a path, a bare name, or a
  # member of a cited family?
  log_cited() {
    local rel="${1#logs/}" pat stem
    printf '%s\n' "$logkeep" | grep -qxF "$rel" && return 0
    printf '%s\n' "$logkeep" | grep -qxF "$(basename "$1")" && return 0
    while IFS= read -r pat; do
      [ -n "$pat" ] || continue
      case "$rel" in $pat) return 0;; esac
    done <<< "$logglobs"
    # The stem before the first `_` (`v9d2` in `v9d2_stdout.txt`) is what a
    # shorthand range names, and an endpoint of one is a file-name prefix when
    # the range's own name carries underscores (`..._end_p1..p4.png`).
    stem="${rel%%_*}"
    printf '%s\n' "$logstems" | grep -qxF "$stem" && return 0
    while IFS= read -r pat; do
      [ -n "$pat" ] || continue
      case "$rel" in "$pat"*) return 0;; esac
    done <<< "$logstems"
    return 1
  }

  echo "logs/ older than $LOG_DAYS days (doc-cited files kept):"
  while IFS= read -r -d '' f; do
    if log_cited "$f"; then
      echo "  kept (cited in docs): $f"
      continue
    fi
    rm_path "$f"
  done < <(find logs -maxdepth 1 -type f -mtime +"$LOG_DAYS" -print0)
  # shots/ follow the same age rule and the same citation guard
  while IFS= read -r -d '' f; do
    if log_cited "$f"; then
      echo "  kept (cited in docs): $f"
      continue
    fi
    rm_path "$f"
  done < <(find logs/shots -type f -mtime +"$LOG_DAYS" -print0 2>/dev/null)
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
