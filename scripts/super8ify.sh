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
#   FILM     [kodak_kodachrome_64]  film stock; "none" = scalar engine
#   AGE      [0.25]                 0 fresh .. 1 attic
#   SEED     [7]                    grain seed
#   FPS      [18]                   Super 8 ran at 18
#   HEIGHT   [720]                  proxy height; real Super 8 resolved less
#   WOW      [14]                   wow depth in cents (slow speed wobble)
#   FLUTTER  [10]                    flutter depth in cents (fast wobble)
#   SILENT   [no]                   yes = period-correct silence, skip audio
#   SUPERJUICE, AUDIOTARD           paths to the binaries if not on PATH
#
# Requires: ffmpeg, ffprobe, superjuice (polajuice repo), audiotard.

set -eu

[ "$#" -ge 1 ] || { echo "usage: $0 INPUT.MOV [OUTPUT.mp4]" >&2; exit 2; }
IN=$1
[ -f "$IN" ] || { echo "super8ify: no such file: $IN" >&2; exit 1; }

CAMERA=${CAMERA:-super8}
FILM=${FILM:-kodak_kodachrome_64}

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
WOW=${WOW:-14}
FLUTTER=${FLUTTER:-10}
SILENT=${SILENT:-no}

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

echo "== reel: $IN -> $OUT ($CAMERA / $FILM, age $AGE, ${FPS} fps)"

# ---- picture ----------------------------------------------------------
ffmpeg -v error -i "$IN" -vf "fps=$FPS,scale=-2:$HEIGHT" \
       -f yuv4mpegpipe -pix_fmt yuv420p - \
  | ( cd "$SJ_DIR" && \
      if [ "$FILM" = "none" ]; then \
          ./superjuice -c "$CAMERA" --no-film --age "$AGE" --seed "$SEED"; \
      else \
          ./superjuice -c "$CAMERA" -f "$FILM" --age "$AGE" --seed "$SEED"; \
      fi ) \
  | ffmpeg -v error -y -f yuv4mpegpipe -i - \
       -vf 'crop=trunc(iw/2)*2:trunc(ih/2)*2' \
       -c:v libx264 -crf 18 -pix_fmt yuv420p "$WORK/reel.mp4"

# ---- sound ------------------------------------------------------------
has_audio=$(ffprobe -v error -select_streams a -show_entries stream=codec_type \
            -of csv=p=0 "$IN" | head -1)

if [ "$SILENT" = "yes" ] || [ -z "$has_audio" ]; then
    [ -z "$has_audio" ] && echo "== no audio track; silent reel (period-correct anyway)"
    [ "$SILENT" = "yes" ] && echo "== SILENT=yes; skipping the stripe"
    mv "$WORK/reel.mp4" "$OUT"
else
    [ -x "$AUDIOTARD" ] || { echo "super8ify: audiotard not found ($AUDIOTARD); set AUDIOTARD= or SILENT=yes" >&2; exit 1; }
    echo "== striping the sound"
    ffmpeg -v error -i "$IN" -vn -ac 1 -ar 44100 "$WORK/audio.wav"
    "$AUDIOTARD" "$WORK/audio.wav" "$WORK/stripe.wav" \
        --tape --shape tube --drive 2 \
        --eq hp:120:0.7:0 --eq lp:5000:0.7:0 \
        --wow-cents "$WOW" --flutter-cents "$FLUTTER" \
        --hiss-db -38 --match-rms
    ffmpeg -v error -y -i "$WORK/reel.mp4" -i "$WORK/stripe.wav" \
        -map 0:v -map 1:a -c:v copy -c:a aac -b:a 96k -shortest "$OUT"
fi

echo "== done: $OUT"
