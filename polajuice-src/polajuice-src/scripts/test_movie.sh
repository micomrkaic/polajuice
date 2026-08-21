#!/bin/sh
# Movie front-end test: pipe a short synthetic clip through superjuice and
# verify output geometry, frame count, and per-frame variation. Uses
# ffmpeg when available; skips cleanly (exit 0) when it is not.
set -eu
cd "$(dirname "$0")/.."
command -v ffmpeg >/dev/null 2>&1 || {
    echo "test_movie: ffmpeg not found, skipping"; exit 0; }
[ -x ./superjuice ] || { echo "test_movie: build superjuice first" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

ffmpeg -v error -f lavfi -i "testsrc2=size=320x240:rate=12:duration=1" \
    -f yuv4mpegpipe -pix_fmt yuv420p "$TMP/in.y4m"
./superjuice -c super8 --no-film --seed 7 \
    < "$TMP/in.y4m" > "$TMP/out.y4m" 2>"$TMP/log"

head -1 "$TMP/out.y4m" | grep -aq "^YUV4MPEG2 W320 H235 C444" || {
    echo "test_movie: bad output header (super8 gate should give 320x235):"
    head -1 "$TMP/out.y4m"; exit 1; }
frames=$(ffprobe -v error -count_frames -select_streams v \
    -show_entries stream=nb_read_frames -of csv=p=0 "$TMP/out.y4m")
[ "$frames" -eq 12 ] || { echo "test_movie: expected 12 frames, got $frames"; exit 1; }

# consecutive frames must differ (boiling grain + weave), same run must be
# deterministic
./superjuice -c super8 --no-film --seed 7 \
    < "$TMP/in.y4m" > "$TMP/out2.y4m" 2>/dev/null
cmp -s "$TMP/out.y4m" "$TMP/out2.y4m" || {
    echo "test_movie: run not deterministic"; exit 1; }
./superjuice -c super8 --no-film --seed 8 \
    < "$TMP/in.y4m" > "$TMP/out3.y4m" 2>/dev/null
cmp -s "$TMP/out.y4m" "$TMP/out3.y4m" && {
    echo "test_movie: seed had no effect"; exit 1; }

# instant cameras must refuse movies
if ./superjuice -c polaroid-600 --no-film < "$TMP/in.y4m" \
        > /dev/null 2>"$TMP/refuse"; then
    echo "test_movie: instant camera was not refused"; exit 1
fi
grep -q "still-print" "$TMP/refuse" || {
    echo "test_movie: refusal message missing"; exit 1; }

# file mode: one command in, playable mp4 with audio passthrough out
ffmpeg -v error -y -f lavfi -i "testsrc2=size=320x240:rate=12:duration=1" \
    -f lavfi -i "sine=frequency=440:duration=1" \
    -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$TMP/clip.mp4"
( cd "$TMP" && "$OLDPWD/superjuice" clip.mp4 -c super8 --no-film --seed 7 \
    2>/dev/null )
[ -f "$TMP/clip_super8.mp4" ] || { echo "test_movie: file mode wrote nothing"; exit 1; }
streams=$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 \
    "$TMP/clip_super8.mp4" | sort | tr '\n' ' ')
echo "$streams" | grep -q audio || { echo "test_movie: audio lost"; exit 1; }
echo "$streams" | grep -q video || { echo "test_movie: video lost"; exit 1; }

echo "movie front-end tests passed ($frames frames, gate 320x235, file mode with audio)"
