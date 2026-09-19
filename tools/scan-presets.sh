#!/bin/bash
#
# Render every preset and record how much of the frame it actually fills.
#
# Reverie's curated default is chosen by a static cost proxy, which says nothing about whether a
# preset draws anything at all. Sampling found that some do not - blank on every machine, for
# reasons ranging from a texture we deliberately do not ship to presets that are simply broken.
# The only way to know which is to render each one and look, so this renders all of them.
#
# It is deliberately dull and interruptible:
#
#   * every result is appended to the output file the moment it is known, so the file *is* the
#     checkpoint - there is no separate state to corrupt
#   * re-running skips everything already recorded, so Ctrl-C, a reboot or a crash costs at most
#     the preset that was in flight
#   * it needs no terminal of its own; run it under nohup, screen, tmux or systemd-run and walk
#     away
#
# Usage:
#   tools/scan-presets.sh [-o results.tsv] [-p presets_dir] [-j jobs] [-s seconds] [-b binary]
#
#   -o  where to append results          (default: preset-scan.tsv)
#   -p  preset root to scan              (default: assets/presets)
#   -j  parallel workers                 (default: 3)
#   -s  seconds to render each preset    (default: 6)
#   -b  the reverie binary to use        (default: build/player/reverie, else /usr/bin/reverie)
#
# Output is TSV: <non-black percent>  <relative path>
# A value of NA means the run produced no reading at all, which is itself worth investigating.
#
# Running several workers at once costs a little accuracy - three on a four-core box read a
# reference preset at 64-66% where one alone read 76%, because each gets fewer frames to
# establish in. That is harmless for finding presets that draw *nothing*, which is the point,
# but it means a borderline reading is not a verdict: re-run anything you intend to act on with
# -j 1 before believing it.
#
# Progress, and what is left, at any time:
#   wc -l results.tsv ; tools/scan-presets.sh -o results.tsv -j 0
set -u

OUT=preset-scan.tsv
ROOT=assets/presets
JOBS=3
SECS=6
BIN=
while getopts "o:p:j:s:b:h" opt; do
  case $opt in
    o) OUT=$OPTARG ;;
    p) ROOT=$OPTARG ;;
    j) JOBS=$OPTARG ;;
    s) SECS=$OPTARG ;;
    b) BIN=$OPTARG ;;
    h) sed -n '2,40p' "$0"; exit 0 ;;
    *) exit 2 ;;
  esac
done

[ -n "$BIN" ] || BIN=$(ls build/player/reverie 2>/dev/null || command -v reverie || echo /usr/bin/reverie)
[ -x "$BIN" ] || { echo "no reverie binary; pass -b" >&2; exit 2; }
[ -d "$ROOT" ] || { echo "no preset directory at $ROOT" >&2; exit 2; }
for need in Xvfb xdpyinfo; do
  command -v $need >/dev/null || { echo "missing $need (apt install xvfb x11-utils)" >&2; exit 2; }
done

WORK=$(mktemp -d)
XPIDS=""
cleanup() {
    for p in $XPIDS; do kill "$p" 2>/dev/null; done
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

# A short tone to drive the visualiser. Presets react to audio, and an idle visualiser renders
# black by design - scanning without sound would report every preset as blank.
TONE=$WORK/tone.wav
if command -v gst-launch-1.0 >/dev/null; then
  gst-launch-1.0 -q audiotestsrc num-buffers=1200 wave=0 freq=220 ! audioconvert ! audioresample \
    ! wavenc ! filesink location="$TONE" >/dev/null 2>&1
fi
[ -s "$TONE" ] || { echo "could not build a test tone (needs gstreamer1.0-tools)" >&2; exit 2; }

touch "$OUT"
# Self-healing: a run interrupted mid-flight can leave a half-written line, and workers that
# outlive the parent by a moment can append a path that was already recorded. Keep the last
# reading for each path and drop anything malformed, so the checkpoint never rots.
# NA is not a result, it is a run that produced nothing - usually because the process was
# interrupted. Dropping those rows means the next resume scans them again rather than carrying a
# hole forward as though it were data.
awk -F'\t' 'NF==2 && $2!="" && $1!="NA" {last[$2]=$1} END {for (k in last) printf "%s\t%s\n", last[k], k}' \
    "$OUT" | LC_ALL=C sort -k2 > "$WORK/clean.tsv"
mv "$WORK/clean.tsv" "$OUT"

find "$ROOT" -name '*.milk' -printf '%P\n' | LC_ALL=C sort > "$WORK/all.txt"
cut -f2 "$OUT" | LC_ALL=C sort -u > "$WORK/done.txt"
LC_ALL=C comm -23 "$WORK/all.txt" "$WORK/done.txt" > "$WORK/todo.txt"

TOTAL=$(wc -l < "$WORK/all.txt")
LEFT=$(wc -l < "$WORK/todo.txt")
echo "presets: $TOTAL   already recorded: $((TOTAL-LEFT))   remaining: $LEFT"
[ "$JOBS" = "0" ] && exit 0
[ "$LEFT" -eq 0 ] && { echo "nothing to do"; exit 0; }

split -n "l/$JOBS" -d "$WORK/todo.txt" "$WORK/part."
worker() {
  local part=$1 disp=$2
  local cfg=$WORK/cfg$disp data=$WORK/data$disp
  mkdir -p "$cfg" "$data"
  Xvfb ":$disp" -screen 0 800x500x24 >/dev/null 2>&1 &
  local xpid=$!
  echo "$xpid" >> "$WORK/xpids"
  for _ in $(seq 1 40); do DISPLAY=":$disp" xdpyinfo >/dev/null 2>&1 && break; sleep 0.5; done
  while read -r rel; do
    [ -z "$rel" ] && continue
    # Each worker gets no session bus at all. Reverie is single-instance by MPRIS bus name, so
    # workers sharing a bus would hand their arguments to the first one and exit - which shows up
    # as an empty reading, not an error. Without a bus it logs that media keys are unavailable
    # and runs normally, which is exactly what a scan wants.
    v=$(DISPLAY=":$disp" XDG_DATA_HOME="$data" XDG_CONFIG_HOME="$cfg" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent/reverie-scan-$disp" \
        PLAYER_AUDIO_SINK=fakesink PLAYER_PROBE=1 \
        timeout $((SECS + 30)) "$BIN" --presets "$ROOT" --preset "$ROOT/$rel" \
        --seconds "$SECS" "$TONE" 2>&1 \
        | sed -n 's/.*non-black [0-9]*\/[0-9]* *(\([0-9]*\)%).*/\1/p' | sort -n | tail -1)
    # One line, appended, flushed: the file is the checkpoint.
    printf '%s\t%s\n' "${v:-NA}" "$rel" >> "$OUT"
  done < "$part"
  kill $xpid 2>/dev/null
}

d=80
for part in "$WORK"/part.*; do
  worker "$part" "$d" &
  d=$((d+1))
done
wait
[ -f "$WORK/xpids" ] && XPIDS=$(tr '\n' ' ' < "$WORK/xpids")
echo "done - $(wc -l < "$OUT") of $TOTAL recorded in $OUT"
