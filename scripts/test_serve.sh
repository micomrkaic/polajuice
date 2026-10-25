#!/bin/sh
# polajuice serve: start it on a scratch folder, exercise every endpoint
# with curl, compare a served render against the CLI byte for byte, and
# stop it. Skips when curl is missing.
set -eu
cd "$(dirname "$0")/.."
command -v curl >/dev/null 2>&1 || { echo "test_serve: curl not found, skipping"; exit 0; }
[ -x ./polajuice ] || { echo "test_serve: build polajuice first" >&2; exit 1; }
TMP=$(mktemp -d)
PORT=$((20000 + $$ % 20000))
cleanup() { [ -n "${PID:-}" ] && { kill "$PID"; wait "$PID" || true; } 2>/dev/null; rm -rf "$TMP"; rm -rf looks/__test_serve__.recipe; }
trap cleanup EXIT
mkdir -p "$TMP/dir"
cp web/samples/sleeping-cat.jpg "$TMP/dir/cat.jpg"
cp tests/raw/bayer_lj92.dng "$TMP/dir/tiny.dng"
./polajuice serve "$TMP/dir" --port "$PORT" > "$TMP/log" 2>&1 &
PID=$!
for i in 1 2 3 4 5 6 7 8 9 10; do
    curl -s "http://127.0.0.1:$PORT/api/version" > "$TMP/v" 2>/dev/null && break
    sleep 0.3
done
grep -q '"native":true' "$TMP/v" || { echo "test_serve: server did not come up"; cat "$TMP/log"; exit 1; }
curl -s "http://127.0.0.1:$PORT/api/cameras" | grep -q '"name":"super8"' || { echo "test_serve: cameras endpoint"; exit 1; }
curl -s "http://127.0.0.1:$PORT/api/files" | grep -q 'cat.jpg' || { echo "test_serve: files endpoint"; exit 1; }
curl -s -o "$TMP/page" -w '%{content_type}' "http://127.0.0.1:$PORT/" | grep -q text/html || { echo "test_serve: page not served"; exit 1; }
curl -s -o "$TMP/dev.jpg" "http://127.0.0.1:$PORT/api/develop?name=tiny.dng&max=48"
[ -s "$TMP/dev.jpg" ] || { echo "test_serve: develop of a raw failed"; exit 1; }
# the served render equals the CLI render of the same look
curl -s -o "$TMP/srv.jpg" -X POST "http://127.0.0.1:$PORT/api/render?name=cat.jpg&camera=super8&film=none&ev=-0.5&halation=1.5&grain=silver&edge=1&seed=7&filter=orange"
./polajuice apply "$TMP/dir/cat.jpg" -c super8 --no-film --ev -0.5 --halation 1.5 --grain silver --edge 1 --seed 7 --filter orange -o "$TMP/cli.jpg" >/dev/null 2>&1
cmp -s "$TMP/srv.jpg" "$TMP/cli.jpg" || { echo "test_serve: served render differs from the CLI"; exit 1; }
# crop and the extended development reach the engine (url-encoded '+')
curl -s -o "$TMP/srv2.jpg" -X POST "http://127.0.0.1:$PORT/api/render?name=cat.jpg&camera=super8&film=none&crop=full&develop=push%2B3&seed=7"
./polajuice apply "$TMP/dir/cat.jpg" -c super8 --no-film --crop full --develop push+3 --seed 7 -o "$TMP/cli2.jpg" >/dev/null 2>&1
cmp -s "$TMP/srv2.jpg" "$TMP/cli2.jpg" || { echo "test_serve: crop/develop render differs from the CLI"; exit 1; }
curl -s "http://127.0.0.1:$PORT/api/cameras" | grep -q '"format":"1.36:1 gate"' || { echo "test_serve: camera list lacks the format label"; exit 1; }
# the inspect numbers for a file in the folder
curl -s "http://127.0.0.1:$PORT/api/stats?name=tiny.dng" | grep -q '"scene_linear":true' || { echo "test_serve: stats of a raw lacks scene_linear"; exit 1; }
curl -s "http://127.0.0.1:$PORT/api/stats?name=cat.jpg" | grep -q '"stops_below_white"' || { echo "test_serve: stats of a jpeg lacks the mean"; exit 1; }
# a dropped image travels in the body
curl -s -o "$TMP/body.jpg" -X POST --data-binary "@$TMP/dir/cat.jpg" "http://127.0.0.1:$PORT/api/render?ext=jpg&camera=super8&film=none&ev=-0.5&halation=1.5&grain=silver&edge=1&seed=7&filter=orange"
cmp -s "$TMP/body.jpg" "$TMP/cli.jpg" || { echo "test_serve: body render differs"; exit 1; }
# export writes next to the source; batch skips exports
curl -s -X POST "http://127.0.0.1:$PORT/api/export?name=cat.jpg&camera=super8&film=none" | grep -q '"written"' || { echo "test_serve: export"; exit 1; }
[ -f "$TMP/dir/cat_super8.jpg" ] || { echo "test_serve: export file missing"; exit 1; }
curl -s "http://127.0.0.1:$PORT/api/files" | grep -q 'cat_super8' && { echo "test_serve: export listed as a source"; exit 1; }
curl -s -X POST "http://127.0.0.1:$PORT/api/batch?camera=bw-35&film=none" | grep -q '"count":2' || { echo "test_serve: batch count"; exit 1; }
# looks as files
printf 'camera=super8\nev=1\n' | curl -s -X PUT --data-binary @- "http://127.0.0.1:$PORT/api/looks?name=__test_serve__" | grep -q '"ok"' || { echo "test_serve: save look"; exit 1; }
[ -f looks/__test_serve__.recipe ] || { echo "test_serve: look file missing"; exit 1; }
curl -s "http://127.0.0.1:$PORT/api/looks" | grep -q '__test_serve__' || { echo "test_serve: looks listing"; exit 1; }
curl -s -X DELETE "http://127.0.0.1:$PORT/api/looks?name=__test_serve__" | grep -q '"ok"' || { echo "test_serve: delete look"; exit 1; }
# refusals
curl -s "http://127.0.0.1:$PORT/api/file?name=../Makefile" | grep -q error || { echo "test_serve: path escape accepted"; exit 1; }
curl -s -X POST "http://127.0.0.1:$PORT/api/render?name=cat.jpg&camera=nope" | grep -q error || { echo "test_serve: bad camera accepted"; exit 1; }
echo "serve ok: endpoints, served render byte-identical to the CLI, export, batch, looks, refusals"
