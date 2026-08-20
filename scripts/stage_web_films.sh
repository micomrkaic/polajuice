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

CANONICAL="polaroid_px-680 polaroid_px-70 polaroid_time_zero_expired \
polaroid_669 kodak_portra_400 kodak_ektachrome_100_vs kodak_kodachrome_64 \
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

blurb_for() {
    case "$1" in
        polaroid_px-680) echo "Impossible/Polaroid 600-era integral film: creamy highlights, gentle color" ;;
        polaroid_px-70) echo "SX-70-era integral film: warmer, dreamier, lower contrast than 600" ;;
        polaroid_time_zero_expired) echo "Expired Time-Zero: shifted, unpredictable, beautifully broken" ;;
        polaroid_669) echo "Peel-apart pack film: punchy contrast, the Land-camera classic" ;;
        kodak_kodachrome_64) echo "The archive look: restrained, deep reds, 1940s-70s slides" ;;
        kodak_portra_400) echo "The portrait workhorse: warm, forgiving skin tones, soft saturation" ;;
        kodak_ektachrome_100_vs) echo "Vivid-saturation slide film: punchy color, clean blues" ;;
        fuji_superia_800) echo "High-speed consumer negative: cool greens, party-photo color" ;;
        fuji_provia_100f) echo "Neutral professional slide: accurate, restrained, fine detail" ;;
        ilford_hp_5_plus_400) echo "Classic ISO 400 black-and-white: medium contrast, honest tonality" ;;
        lomography_x-pro_slide_200) echo "Cross-processed slide look: shifted colors, heavy contrast" ;;
        *) echo "" ;;
    esac
}

# films.json: a JSON array of {stem, blurb} objects
{
    printf '['
    first=1
    for stem in $staged; do
        [ "$first" -eq 1 ] || printf ','
        printf '{"stem":"%s","blurb":"%s"}' "$stem" "$(blurb_for "$stem")"
        first=0
    done
    printf ']\n'
} > "$DEST/films.json"

echo "staged$staged"
echo "manifest: $DEST/films.json"
