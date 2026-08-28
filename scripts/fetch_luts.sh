#!/bin/sh
# fetch_luts.sh - download film-stock 3D LUTs matching Polajuice's presets.
#
# Default: fetch the G'MIC color-preset family packs, published directly in
# .cube format at https://gmic.eu/color_presets/<family>/<family>_cube.zip
# (verified live 2026-08). Each family covers one or more built-in presets:
#
#   instant_consumer  polaroid-600, polaroid-sx70   Px-680/Px-70/Px-100UV/Time Zero
#   instant_pro       polaroid-packfilm       Polaroid 665/669/690, Fuji FP
#   colorslide        35mm-slide, super8       Ektachrome/Provia/Velvia...
#   bw                bw-35                   HP5 and other B/W stocks
#   negative_old      disposable-flash        Superia 100..1600 etc.
#   negative_new      35mm-negative           Portra/Fuji C-series etc.
#
# Optional: fetch_luts.sh --rawtherapee additionally downloads the
# RawTherapee Film Simulation Collection (~402 MB of HaldCLUT PNGs,
# hundreds of stocks) and converts every PNG to .cube with
# tools/haldclut2cube.py (requires python3 + Pillow).
#
# Everything lands in data/luts/<source>/. Idempotent: existing files are
# kept unless --force is given.

set -u

BASE_GMIC="https://gmic.eu/color_presets"
URL_RT="https://rawtherapee.com/shared/HaldCLUT.zip"
DEST="${LUT_DIR:-data/luts}"
FAMILIES="instant_consumer instant_pro colorslide bw negative_old negative_new"

FORCE=0 RT=0
for arg in "$@"; do
    case "$arg" in
        --force)       FORCE=1 ;;
        --rawtherapee) RT=1 ;;
        -h|--help)     sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v curl  >/dev/null || { echo "curl is required" >&2; exit 1; }
command -v unzip >/dev/null || { echo "unzip is required" >&2; exit 1; }

failures=0

fetch_family() {
    family="$1"
    dir="$DEST/$family"
    zip="$dir/${family}_cube.zip"
    if [ -d "$dir" ] && [ "$FORCE" -eq 0 ] && \
       [ -n "$(find "$dir" -name '*.cube' -print -quit 2>/dev/null)" ]; then
        echo "== $family: already present, skipping (use --force to refetch)"
        return 0
    fi
    mkdir -p "$dir"
    echo "== $family: downloading ${family}_cube.zip"
    if ! curl -fSL --retry 3 --connect-timeout 15 \
              "$BASE_GMIC/$family/${family}_cube.zip" -o "$zip"; then
        echo "!! $family: download failed" >&2
        failures=$((failures + 1))
        return 1
    fi
    if ! unzip -oq "$zip" -d "$dir"; then
        echo "!! $family: unzip failed" >&2
        failures=$((failures + 1))
        return 1
    fi
    rm -f "$zip"
    echo "   $family: $(find "$dir" -name '*.cube' | wc -l) cube files"
}

for family in $FAMILIES; do
    fetch_family "$family"
done

if [ "$RT" -eq 1 ]; then
    command -v python3 >/dev/null || { echo "python3 required for --rawtherapee" >&2; exit 1; }
    dir="$DEST/rawtherapee"
    mkdir -p "$dir"
    zip="$dir/HaldCLUT.zip"
    if [ ! -f "$zip" ] || [ "$FORCE" -eq 1 ]; then
        echo "== rawtherapee: downloading HaldCLUT.zip (~402 MB, be patient)"
        curl -fSL --retry 3 --connect-timeout 15 "$URL_RT" -o "$zip" || {
            echo "!! rawtherapee: download failed" >&2; failures=$((failures + 1));
        }
    fi
    if [ -f "$zip" ]; then
        unzip -oq "$zip" -d "$dir" || failures=$((failures + 1))
        echo "== rawtherapee: converting HaldCLUT PNGs to .cube"
        # cubes land FLAT in $dir with house-style stems: lowercase,
        # spaces -> underscores, parentheses dropped - so catalog globs
        # match, shell loops never split, and the CLI resolves them.
        find "$dir" -name '*.png' | while read -r png; do
            stem=$(basename "$png" .png | tr 'A-Z ' 'a-z_' | tr -d "()'" )
            cube="$dir/$stem.cube"
            [ -f "$cube" ] && [ "$FORCE" -eq 0 ] && continue
            python3 "$(dirname "$0")/../tools/haldclut2cube.py" "$png" "$cube" \
                || echo "!! conversion failed: $png" >&2
        done
        stray=$(find "$dir" -mindepth 2 -name '*.cube' | wc -l)
        [ "$stray" -gt 0 ] && echo "== note: $stray old nested .cube files remain from a previous run;" \
            "remove with: find $dir -mindepth 2 -name '*.cube' -delete" >&2
    fi
fi

echo
if [ "$failures" -gt 0 ]; then
    echo "finished with $failures failure(s); rerun to retry" >&2
    exit 1
fi
echo "all LUTs in $DEST/; use e.g.:"
echo "  ./polajuice apply in.ppm -p polaroid-600 \\"
echo "      --lut $DEST/instant_consumer/polaroid_px-680.cube -o out.ppm"
