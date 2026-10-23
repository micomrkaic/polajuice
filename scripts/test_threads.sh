#!/bin/sh
# Thread determinism: every banded stage must produce the same bytes at
# any thread count. Renders a sample through the heaviest path (silver
# grain, edge effects, halation, the scalar engine) at 1, 2 and 5
# threads and compares.
set -eu
cd "$(dirname "$0")/.."
[ -x ./polajuice ] || { echo "test_threads: build polajuice first" >&2; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
IN=web/samples/sleeping-cat.jpg
[ -f "$IN" ] || IN=tests/raw/linear16.png
for n in 1 2 5; do
    POLAJUICE_THREADS=$n ./polajuice apply "$IN" -c super8 --no-film --seed 7 \
        --grain silver --edge 1.5 --halation 1.5 -o "$TMP/t$n.png" >/dev/null 2>&1
done
cmp -s "$TMP/t1.png" "$TMP/t2.png" || { echo "test_threads: 1 vs 2 threads differ"; exit 1; }
cmp -s "$TMP/t1.png" "$TMP/t5.png" || { echo "test_threads: 1 vs 5 threads differ"; exit 1; }
# the JPEG encoder and the raw decoder band too
for n in 1 3; do
    POLAJUICE_THREADS=$n ./polajuice develop tests/raw/bayer_lj92.dng -o "$TMP/d$n.jpg" >/dev/null 2>&1
    POLAJUICE_THREADS=$n ./polajuice apply "$IN" -c bw-35 --no-film --seed 7 -o "$TMP/j$n.jpg" >/dev/null 2>&1
done
cmp -s "$TMP/d1.jpg" "$TMP/d3.jpg" || { echo "test_threads: raw decode differs by thread count"; exit 1; }
cmp -s "$TMP/j1.jpg" "$TMP/j3.jpg" || { echo "test_threads: JPEG output differs by thread count"; exit 1; }
echo "thread determinism ok (1, 2, 5 threads byte-identical; raw decode and JPEG too)"
