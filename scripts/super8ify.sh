#!/bin/sh
# super8ify.sh - phone clip in, 1970s Super 8 reel with magnetic-stripe
# sound out. Video through superjuice (18 fps, 720p, Kodachrome, aged),
# audio through audiotard (tape chain, projector-speaker bandwidth, small
# tube amp), married by ffmpeg at the end.
#
#   usage: super8ify.sh INPUT.MOV [OUTPUT.mp4]
#
# Tunables (environment variables, defaults in brackets):
#   CAMERA   [super8]               polajuice camera
#   FILM     [camera's canonical]  film stock; unset lets superjuice pick
#                                  the camera's own default (sealed cameras
#                                  take none); "none" forces scalar engine;
#                                  FILM=kodak_kodachrome_64 says 1962
#   AGE      [0.25]                 0 fresh .. 1 attic
#   SEED     [7]                    grain seed
#   FPS      [18]                   Super 8 ran at 18
#   HEIGHT   [720]                  proxy height; real Super 8 resolved less
#   FILTER   []                    lens contrast filter (yellow, orange,
#                                   red, green, blue); orange + a bw
#                                   camera is the dramatic-sky classic
#   MOTION   [1]                   gate weave / flicker scale: 0.5 steadier,
#                                   2 = worn projector, 0 = tripod-locked
#   SOUND    [stripe]              stripe = audiotard tape mangle;
#                                   clean = original audio untouched;
#                                   silent = no audio (period-correct)
#   WOW      [14]                   wow depth in cents (slow speed wobble)
#   FLUTTER  [10]                   flutter depth in cents (fast wobble)
#   HISS     [-38]                  tape hiss level in dBFS
#   DRIVE    [2]                    tube drive amount into the small amp
#   SHAPE    [tube]                 audiotard --shape curve
#   BAND     [120:5000]             projector-speaker bandwidth lo:hi Hz
#   AT_EXTRA []                     extra audiotard args verbatim, e.g.
#                                   AT_EXTRA="--vinyl" for the wrong era
#   SILENT   [no]                   yes = alias for SOUND=silent (compat)
#   SUPERJUICE, AUDIOTARD           paths to the binaries if not on PATH
#
# Requires: ffmpeg, ffprobe, superjuice (polajuice repo), audiotard.

set -eu

[ "$#" -ge 1 ] || { echo "usage: $0 INPUT.MOV [OUTPUT.mp4]" >&2; exit 2; }
IN=$1
[ -f "$IN" ] || { echo "super8ify: no such file: $IN" >&2; exit 1; }

CAMERA=${CAMERA:-super8}
FILM=${FILM:-}

base=$(basename "$IN")
stem=${base%.*}
OUT=${2:-"${stem}_${CAMERA}.mp4"}
case "$OUT" in *=*)
    echo "super8ify: output name '$OUT' contains '=' - did you mean to put" >&2
    echo "  VAR=value assignments BEFORE the command? e.g.:" >&2
    echo "  CAMERA=polavision FILM=none $0 $IN" >&2
    exit 2 ;;
esac
AGE=${AGE:-0.25}
SEED=${SEED:-7}
FPS=${FPS:-18}
HEIGHT=${HEIGHT:-720}
FILTER=${FILTER:-}
MOTION=${MOTION:-1}
SOUND=${SOUND:-stripe}
WOW=${WOW:-14}
FLUTTER=${FLUTTER:-10}
HISS=${HISS:--38}
DRIVE=${DRIVE:-2}
SHAPE=${SHAPE:-tube}
BAND=${BAND:-120:5000}
AT_EXTRA=${AT_EXTRA:-}
SILENT=${SILENT:-no}
[ "$SILENT" = "yes" ] && SOUND=silent
case "$SOUND" in stripe|clean|silent) ;; *)
    echo "super8ify: SOUND must be stripe, clean or silent (got '$SOUND')" >&2
    exit 2 ;;
esac
BAND_LO=${BAND%%:*}
BAND_HI=${BAND##*:}

# find the tools: env override, then the sibling binary in this repo,
# then PATH, then the house layout
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -z "${SUPERJUICE:-}" ]; then
    if [ -x "$SCRIPT_DIR/../superjuice" ]; then
        SUPERJUICE="$SCRIPT_DIR/../superjuice"
    else
        SUPERJUICE=$(command -v superjuice || echo "$HOME/work/c_progs/polajuice/polajuice/superjuice")
    fi
fi
AUDIOTARD=${AUDIOTARD:-$(command -v audiotard || echo "$HOME/work/c_progs/audiotard/audiotard")}
[ -x "$SUPERJUICE" ] || { echo "super8ify: superjuice not found ($SUPERJUICE); set SUPERJUICE=" >&2; exit 1; }
command -v ffmpeg >/dev/null 2>&1 || { echo "super8ify: ffmpeg not found" >&2; exit 1; }

# superjuice resolves films relative to its repo; run it from there
SJ_DIR=$(dirname "$SUPERJUICE")

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "== reel: $IN -> $OUT ($CAMERA / ${FILM:-canonical}, age $AGE, ${FPS} fps, sound: $SOUND)"

# ---- picture ----------------------------------------------------------
ffmpeg -v error -i "$IN" -vf "fps=$FPS,scale=-2:$HEIGHT" \
       -f yuv4mpegpipe -pix_fmt yuv420p - \
  | ( cd "$SJ_DIR" && \
      case "$FILM" in \
      none) ./superjuice -c "$CAMERA" --no-film --age "$AGE" --seed "$SEED" --motion-scale "$MOTION" ${FILTER:+--filter "$FILTER"} ;; \
      "")   ./superjuice -c "$CAMERA" --age "$AGE" --seed "$SEED" --motion-scale "$MOTION" ${FILTER:+--filter "$FILTER"} ;; \
      *)    ./superjuice -c "$CAMERA" -f "$FILM" --age "$AGE" --seed "$SEED" --motion-scale "$MOTION" ${FILTER:+--filter "$FILTER"} ;; \
      esac ) \
  | ffmpeg -v error -y -f yuv4mpegpipe -i - \
       -vf 'crop=trunc(iw/2)*2:trunc(ih/2)*2' \
       -c:v libx264 -crf 18 -pix_fmt yuv420p "$WORK/reel.mp4"

# ---- sound ------------------------------------------------------------
has_audio=$(ffprobe -v error -select_streams a -show_entries stream=codec_type \
            -of csv=p=0 "$IN" | head -1)
[ -z "$has_audio" ] && [ "$SOUND" != "silent" ] && {
    echo "== no audio track; silent reel (period-correct anyway)"
    SOUND=silent
}

case "$SOUND" in
silent)
    mv "$WORK/reel.mp4" "$OUT"
    ;;
clean)
    echo "== keeping the sound untouched"
    ffmpeg -v error -y -i "$WORK/reel.mp4" -i "$IN" \
        -map 0:v -map 1:a -c:v copy -c:a copy -shortest "$OUT" 2>/dev/null \
      || ffmpeg -v error -y -i "$WORK/reel.mp4" -i "$IN" \
            -map 0:v -map 1:a -c:v copy -c:a aac -b:a 192k -shortest "$OUT"
    ;;
stripe)
    [ -x "$AUDIOTARD" ] || { echo "super8ify: audiotard not found ($AUDIOTARD); set AUDIOTARD= or SOUND=clean/silent" >&2; exit 1; }
    echo "== striping the sound (band $BAND, wow $WOW, flutter $FLUTTER, hiss $HISS)"
    ffmpeg -v error -i "$IN" -vn -ac 1 -ar 44100 "$WORK/audio.wav"
    "$AUDIOTARD" "$WORK/audio.wav" "$WORK/stripe.wav" \
        --tape --shape "$SHAPE" --drive "$DRIVE" \
        --eq "hp:$BAND_LO:0.7:0" --eq "lp:$BAND_HI:0.7:0" \
        --wow-cents "$WOW" --flutter-cents "$FLUTTER" \
        --hiss-db "$HISS" --match-rms $AT_EXTRA
    ffmpeg -v error -y -i "$WORK/reel.mp4" -i "$WORK/stripe.wav" \
        -map 0:v -map 1:a -c:v copy -c:a aac -b:a 96k -shortest "$OUT"
    ;;
esac

echo "== done: $OUT"
