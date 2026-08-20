#!/bin/sh
# stage_web_films.sh - copy each camera's canonical film cube from the film
# library (data/luts, populated by 'make fetch-luts') into web/films/ and
# write the films.json manifest the web UI reads.
#
# Only the canonical films are staged by default to keep the deployed page
# small; pass extra stems as arguments to include favorites:
#   sh scripts/stage_web_films.sh kodak_portra_800 fuji_velvia_50

set -eu
cd "$(dirname "$0")/.."

LIB="${LUT_DIR:-data/luts}"
DEST="web/films"

[ -d "$LIB" ] || {
    echo "no film library at $LIB; run 'make fetch-luts' first" >&2
    exit 1
}

CANONICAL="polaroid_px-680 kodak_portra_400 kodak_ektachrome_100_vs \
fuji_superia_800 fuji_provia_100f ilford_hp_5_plus_400 \
lomography_x-pro_slide_200"

mkdir -p "$DEST"
staged=""
for stem in $CANONICAL "$@"; do
    found=$(find "$LIB" -name "$stem.cube" -print -quit)
    if [ -z "$found" ]; then
        echo "!! not in library, skipping: $stem" >&2
        continue
    fi
    cp "$found" "$DEST/$stem.cube"
    staged="$staged $stem"
done

# films.json: a JSON array of staged stems
{
    printf '['
    first=1
    for stem in $staged; do
        [ "$first" -eq 1 ] || printf ','
        printf '"%s"' "$stem"
        first=0
    done
    printf ']\n'
} > "$DEST/films.json"

echo "staged$staged"
echo "manifest: $DEST/films.json"
