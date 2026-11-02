#!/bin/sh
# test_install.sh - 'make install-files' into a scratch prefix, then run
# the installed binaries from an unrelated directory: assets must be
# found through ~/.local/share (XDG_DATA_HOME here), the page must serve,
# and 'make uninstall' must leave nothing behind. No network: the film
# library is a stand-in cube.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
PREFIX="$WORK/prefix"
make -s -C "$ROOT" install-files PREFIX="$PREFIX" >/dev/null
for f in bin/polajuice bin/superjuice bin/super8ify share/polajuice/web/index.html \
         share/polajuice/prints/synthetic_minilab.cube share/man/man1/polajuice.1; do
    [ -e "$PREFIX/$f" ] || { echo "install: missing $PREFIX/$f" >&2; exit 1; }
done
# a stand-in film library where 'make install-luts' would put the real one
mkdir -p "$PREFIX/share/polajuice/luts/bw"
cp "$ROOT/data/prints/synthetic_minilab.cube" "$PREFIX/share/polajuice/luts/bw/kodak_tri-x_400.cube"
# from somewhere else, with nothing in the working directory
mkdir "$WORK/elsewhere"; cd "$WORK/elsewhere"
export XDG_DATA_HOME="$PREFIX/share"; unset POLAJUICE_FILMS POLAJUICE_PRINTS POLAJUICE_WEB POLAJUICE_HOME || true
"$PREFIX/bin/polajuice" paths > paths.txt
grep -q "^films   $PREFIX/share/polajuice/luts$" paths.txt || { echo "paths: film library not resolved:"; cat paths.txt; exit 1; }
grep -q "^web     $PREFIX/share/polajuice/web$" paths.txt || { echo "paths: web root not resolved:"; cat paths.txt; exit 1; }
"$PREFIX/bin/polajuice" films | grep -q "kodak_tri-x_400" || { echo "films: installed library not listed" >&2; exit 1; }
cp "$ROOT/web/samples/"*.jpg . 2>/dev/null || true
first=$(ls *.jpg | head -1)
"$PREFIX/bin/polajuice" apply "$first" -c bw-35 -f kodak_tri-x_400 -o out.jpg >/dev/null
[ -s out.jpg ] || { echo "apply: no output from the installed binary" >&2; exit 1; }
"$PREFIX/bin/polajuice" apply "$first" -c 35mm-negative --no-film --print synthetic_minilab -o print.jpg >/dev/null
[ -s print.jpg ] || { echo "apply: installed print stock not found" >&2; exit 1; }
# the page, served from the install
"$PREFIX/bin/polajuice" serve . --port 18093 >serve.log 2>&1 &
pid=$!
ok=0
for i in 1 2 3 4 5 6 7 8 9 10; do
    if curl -fs http://127.0.0.1:18093/index.html | grep -q "Polajuice"; then ok=1; break; fi
    sleep 0.3
done
{ kill $pid; wait $pid || true; } 2>/dev/null
[ "$ok" = 1 ] || { echo "serve: installed page not served:"; cat serve.log; exit 1; }
# $POLAJUICE_HOME relocates the whole set
POLAJUICE_HOME="$PREFIX/share/polajuice" XDG_DATA_HOME=/nonexistent "$PREFIX/bin/polajuice" paths | grep -q "^prints  $PREFIX/share/polajuice/prints$" \
    || { echo "paths: POLAJUICE_HOME ignored" >&2; exit 1; }
make -s -C "$ROOT" uninstall PREFIX="$PREFIX" >/dev/null
left=$(find "$PREFIX" -type f | wc -l)
[ "$left" -eq 0 ] || { echo "uninstall left files:"; find "$PREFIX" -type f; exit 1; }
echo "install ok: binaries, prints, page and man pages under a prefix; assets found from elsewhere; uninstall clean"
