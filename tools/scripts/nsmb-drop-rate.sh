#!/bin/sh
# Does the NSMB link drop out of multiplayer on its own, and how often?
#
# Every run is one pass of the reference route, which owns its own coordinates
# (nsmb-route.sh beside this file); this script only repeats it and reads the
# logs. Each run restores the save first, so the route is timed against the same
# boot every time.
#
# A run "dropped" when the guest's game sent a deauth ([CLIENT] deauth) or a
# radio went off. The emulated offset is derived from the route's repeating tap,
# which fires every 160 frames from 2600 for 6 frames and logs one line per
# console per frame.
set -eu

HERE=$(dirname "$0")
ROUTE=${ROUTE:-$HERE/nsmb-route.sh}
FRAMES=${FRAMES:-8000}
RUNS=${RUNS:-8}
OUT=${OUT:-logs/drop_rate}

mkdir -p "$OUT"
dropped=0
i=1
while [ "$i" -le "$RUNS" ]; do
    log="$OUT/run$i.log"
    FRAMES="$FRAMES" "$ROUTE" > "$log" 2>&1 || true

    deauth=$(grep -c "\[CLIENT\] deauth" "$log" || true)
    radiooff=$(grep -c "wireless off (end #3)" "$log" || true)
    if [ "$deauth" -gt 0 ]; then
        dropped=$((dropped + 1))
        line=$(grep -n "\[CLIENT\] deauth" "$log" | head -1 | cut -d: -f1)
        n=$(head -"$line" "$log" | grep -c "player 1 touch -> 128,171" || true)
        frame=$(( 2600 + (n / 6 - 1) * 160 + 5 ))
        printf 'run %s: DROPPED  (guest deauth, ~frame %s = %.1f emulated s)\n' \
            "$i" "$frame" "$(echo "$frame" | awk '{print $1/59.826}')"
    else
        printf 'run %s: survived (no deauth; radio-off count %s)\n' "$i" "$radiooff"
    fi
    i=$((i + 1))
done

printf 'dropped %s of %s runs\n' "$dropped" "$RUNS"
