#!/bin/bash
# Run Reverie for a long time against a real, mixed playlist and record what it does to memory.
#
# The brief left this open: a 15-minute run measured RSS growing about 0.7 MB/min and said the
# trend was worth re-checking over a multi-hour run before anyone called it fine. This is that
# check, and it is deliberately not a synthetic one - real files, shuffle, repeat, and the kind of
# fiddling a person actually does: panels in and out, fullscreen, resizing, skipping tracks.
#
# Stage transitions are the point. Audio and video share one slot, so every change between them
# tears down one surface and builds the other, and that is where a leak would hide.
#
#   tools/soak-test.sh -m <media dir> -d <seconds> [-i <sample seconds>] [-o <out.tsv>] [-V]
#
#   -V  visualisations OFF - the drifting-logo path instead of projectM.
#
# Never kills by name. Every process it starts is killed by its recorded pid, because a blanket
# pgrep/pkill once destroyed a scan's own workers and cost 1,629 readings.
set -u

MEDIA=; DURATION=3600; INTERVAL=30; OUT=; NOVIS=0; DISP=101; OWNDISPLAY=1
while getopts "m:d:i:o:VD:Xh" opt; do
  case $opt in
    m) MEDIA=$OPTARG ;;
    d) DURATION=$OPTARG ;;
    i) INTERVAL=$OPTARG ;;
    o) OUT=$OPTARG ;;
    V) NOVIS=1 ;;
    D) DISP=$OPTARG ;;
    X) OWNDISPLAY=0 ;;   # use the display already running, rather than a private Xvfb
    h) sed -n '2,20p' "$0"; exit 0 ;;
    *) exit 2 ;;
  esac
done
[ -n "$MEDIA" ] && [ -d "$MEDIA" ] || { echo "need -m <media dir>" >&2; exit 2; }
[ -n "$OUT" ] || OUT=soak-$(date +%H%M).tsv

BIN=$(ls build/player/reverie 2>/dev/null || command -v reverie)
[ -x "$BIN" ] || { echo "no reverie binary" >&2; exit 2; }

WORK=$(mktemp -d)
CFG=$WORK/cfg; DATA=$WORK/data
mkdir -p "$CFG/reverie" "$DATA"
# Shuffle and repeat-the-playlist so it never stops, and the first-run notice suppressed so the
# window is not sitting behind a modal for the whole run.
cat > "$CFG/reverie/reverie.conf" <<EOF
[playlist]
shuffle=true
repeatMode=1
persist=false

[visualisation]
flashNoticeSeen=true
enabled=$([ "$NOVIS" = 1 ] && echo false || echo true)
markBounce=true
EOF

mapfile -t FILES < <(find "$MEDIA" -type f \
  \( -iname '*.mp3' -o -iname '*.flac' -o -iname '*.m4a' -o -iname '*.ogg' -o -iname '*.opus' \
     -o -iname '*.wav' -o -iname '*.mp4' -o -iname '*.mkv' -o -iname '*.webm' -o -iname '*.avi' \) \
  | sort)
[ "${#FILES[@]}" -gt 0 ] || { echo "no media under $MEDIA" >&2; exit 2; }

echo "soak: ${#FILES[@]} files, ${DURATION}s, sampling every ${INTERVAL}s, visuals $([ "$NOVIS" = 1 ] && echo off || echo on)"

# -X runs against the display that is already there. That is the only way to reach a real GPU:
# a private Xvfb falls back to software rendering, so on a machine with hardware acceleration it
# would measure the same thing this VM already measures and learn nothing.
if [ "$OWNDISPLAY" = 1 ]; then
  Xvfb ":$DISP" -screen 0 1280x800x24 >/dev/null 2>&1 &
  XVFB_PID=$!
  for _ in $(seq 1 40); do DISPLAY=":$DISP" xdpyinfo >/dev/null 2>&1 && break; sleep 0.5; done
  # A real window manager, because fullscreen and maximise are its job. Without one the geometry
  # calls silently do nothing, which looks exactly like a test that passed.
  DISPLAY=":$DISP" metacity >/dev/null 2>&1 &
  WM_PID=$!
  sleep 2
else
  [ -n "${DISPLAY:-}" ] || { echo "-X needs DISPLAY set" >&2; exit 2; }
  DISP=${DISPLAY#:}
  XVFB_PID=0; WM_PID=0
  echo "soak: using the existing display :$DISP"
fi

DISPLAY=":$DISP" XDG_DATA_HOME="$DATA" XDG_CONFIG_HOME="$CFG" \
  DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent/reverie-soak-$DISP" \
  PLAYER_AUDIO_SINK=fakesink \
  "$BIN" "${FILES[@]}" > "$WORK/reverie.log" 2>&1 &
APP_PID=$!
sleep 12

kill -0 "$APP_PID" 2>/dev/null || { echo "reverie exited immediately:"; tail -5 "$WORK/reverie.log"; exit 1; }

WID=$(DISPLAY=":$DISP" xwininfo -root -tree 2>/dev/null \
      | grep -oE '0x[0-9a-f]+ .* [0-9]{3,4}x[0-9]{3,4}\+[0-9]+\+[0-9]+' \
      | awk '{print $1}' | head -1)
[ -n "$WID" ] && DISPLAY=":$DISP" xdotool windowactivate "$WID" 2>/dev/null
echo "soak: window $WID, pid $APP_PID, log $WORK/reverie.log"

key()  { [ -n "$WID" ] && DISPLAY=":$DISP" xdotool key --window "$WID" "$1" 2>/dev/null; }
click(){ DISPLAY=":$DISP" xdotool mousemove "$1" "$2" click 1 2>/dev/null; }

FULL=0; MINI=0
# One action per sample, cycled rather than random so a failure is reproducible and every one of
# them is actually exercised - a random walk can skip a case for an hour.
act() {
  case $(( $1 % 10 )) in
    0) key ctrl+l ;;                                   # playlist panel
    1) click 8 400 ;;                                  # visualisation panel, edge handle
    2) key ctrl+l ;;
    3) if [ "$FULL" = 0 ]; then key f; FULL=1; else key Escape; FULL=0; fi ;;
    4) [ "$FULL" = 0 ] && DISPLAY=":$DISP" xdotool windowsize "$WID" 820 600 2>/dev/null ;;
    5) key ctrl+Right ;;                               # next track - forces a stage change
    6) [ "$FULL" = 0 ] && DISPLAY=":$DISP" xdotool windowsize "$WID" 1180 740 2>/dev/null ;;
    7) if [ "$FULL" = 0 ]; then
         if [ "$MINI" = 0 ]; then key ctrl+m; MINI=1; else key ctrl+m; MINI=0; fi
       fi ;;
    8) key n ;;                                        # next visualisation
    9) if [ "$FULL" = 1 ]; then key Escape; FULL=0; else key f; FULL=1; fi ;;
  esac
}

# iowait and the process state are sampled because this VM's host is suspected of poor disk
# health, and a stalled host looks exactly like a stalled application from the inside. State "D"
# is uninterruptible I/O wait: if a pause in the trace lines up with D and a jump in iowait, it
# was the storage underneath, not Reverie. Without this the run can only say "something stalled".
printf 'elapsed\trss_kb\tvmsize_kb\tthreads\tfds\tcpu_pct\txorg_rss_kb\tmaps\tstate\tiowait_pct\tload1\n' > "$OUT"
prev_cpu=0; prev_t=0; prev_iow=0; prev_tot=0; i=0; start=$(date +%s)
while :; do
  now=$(date +%s); elapsed=$(( now - start ))
  [ "$elapsed" -ge "$DURATION" ] && break
  kill -0 "$APP_PID" 2>/dev/null || { echo "soak: reverie died at ${elapsed}s"; break; }

  rss=$(awk '/^VmRSS:/{print $2}' /proc/$APP_PID/status 2>/dev/null)
  vsz=$(awk '/^VmSize:/{print $2}' /proc/$APP_PID/status 2>/dev/null)
  thr=$(awk '/^Threads:/{print $2}' /proc/$APP_PID/status 2>/dev/null)
  fds=$(ls /proc/$APP_PID/fd 2>/dev/null | wc -l)
  maps=$(wc -l < /proc/$APP_PID/maps 2>/dev/null)
  ticks=$(awk '{print $14+$15}' /proc/$APP_PID/stat 2>/dev/null)
  xrss=0
  [ "$XVFB_PID" != 0 ] && xrss=$(awk '/^VmRSS:/{print $2}' /proc/$XVFB_PID/status 2>/dev/null)
  st=$(awk '{print $3}' /proc/$APP_PID/stat 2>/dev/null)
  load1=$(awk '{print $1}' /proc/loadavg 2>/dev/null)
  read -r _ u n sy id iow rest < /proc/stat
  tot=$(( u + n + sy + id + iow ))
  iowpct=0
  if [ "$prev_tot" -gt 0 ] && [ $(( tot - prev_tot )) -gt 0 ]; then
    iowpct=$(( (iow - prev_iow) * 100 / (tot - prev_tot) ))
  fi
  prev_iow=$iow; prev_tot=$tot
  cpu=0
  if [ -n "${ticks:-}" ] && [ "$prev_t" -gt 0 ]; then
    hz=$(getconf CLK_TCK); dt=$(( now - prev_t ))
    [ "$dt" -gt 0 ] && cpu=$(( (ticks - prev_cpu) * 100 / hz / dt ))
  fi
  prev_cpu=${ticks:-0}; prev_t=$now
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$elapsed" "${rss:-0}" "${vsz:-0}" "${thr:-0}" "$fds" "$cpu" "${xrss:-0}" "${maps:-0}" \
    "${st:-?}" "$iowpct" "${load1:-0}" >> "$OUT"

  act "$i"; i=$(( i + 1 ))
  sleep "$INTERVAL"
done

echo "soak: stopping"
kill "$APP_PID" 2>/dev/null; sleep 2; kill -9 "$APP_PID" 2>/dev/null
[ "$WM_PID" != 0 ] && kill "$WM_PID" 2>/dev/null
[ "$XVFB_PID" != 0 ] && kill "$XVFB_PID" 2>/dev/null
grep -icE 'warning|error|could not|failed' "$WORK/reverie.log" \
  | xargs -I{} echo "soak: {} warning/error line(s) in the app log"
cp "$WORK/reverie.log" "${OUT%.tsv}.log" 2>/dev/null
echo "soak: $(wc -l < "$OUT") samples -> $OUT, log -> ${OUT%.tsv}.log"
