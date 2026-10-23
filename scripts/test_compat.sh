#!/bin/sh
# Compatibility matrix test: every camera against a representative stock
# from each process family, asserted cell by cell against the model the
# README documents. Runs a stub film library so no downloads are needed.
set -eu
cd "$(dirname "$0")/.."
[ -x ./polajuice ] || { echo "test_compat: build polajuice first" >&2; exit 1; }

# The library root deliberately contains a /bw/ component: macOS temp
# roots under /var/folders/XX/ once had XX = "bw", which the old
# whole-path family match read as a black-and-white shelf for every
# stock. The matrix below is the regression.
TOP=$(mktemp -d)
trap 'rm -rf "$TOP"' EXIT
LIB="$TOP/bw/library"
mkdir -p "$LIB"
mkdir -p "$LIB/colorslide" "$LIB/negative_new" "$LIB/negative_old" \
         "$LIB/bw" "$LIB/instant_consumer" "$LIB/instant_pro"
cube() { printf 'TITLE "t"\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n' > "$1"; }
cube "$LIB/colorslide/fuji_provia_100f.cube"
cube "$LIB/colorslide/kodak_kodachrome_64.cube"
cube "$LIB/colorslide/kodak_kodachrome_64_generic.cube"
cube "$LIB/negative_new/kodak_portra_400.cube"
cube "$LIB/bw/ilford_hp_5_plus_400.cube"
cube "$LIB/instant_consumer/polaroid_px-100uv+_warm.cube"
cube "$LIB/instant_pro/polaroid_669.cube"
cube "$LIB/negative_old/fuji_neopan_1600.cube"
mkdir -p "$LIB/../prints"
cube "$LIB/../prints/synthetic_cine_print.cube"
export POLAJUICE_PRINTS="$LIB/../prints"

# inside $LIB (cleaned by the trap): BSD mktemp has no --suffix, and the
# extension must be real because format dispatch reads it
IN="$LIB/in.ppm"
OUT="$LIB/out.jpg"
printf 'P6 2 2 255 ' > "$IN"
printf '\200\200\200\200\200\200\200\200\200\200\200\200' >> "$IN"

expect() {  # expect CAMERA FILM allow|refuse
    if POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c "$1" -f "$2" \
            -o "$OUT" >/dev/null 2>&1
    then got=allow; else got=refuse; fi
    [ "$got" = "$3" ] || {
        echo "test_compat: $1 + $2 => $got, expected $3" >&2; exit 1; }
}

# slide      negative     bw        integral      pack
expect 35mm-negative provia refuse;      expect 35mm-negative portra_400 allow
expect 35mm-negative hp_5 refuse;        expect 35mm-negative px-100uv refuse
expect 35mm-slide provia allow;          expect 35mm-slide portra_400 refuse
expect 35mm-slide px-100uv refuse;       expect 35mm-slide 669 refuse
expect bw-35 hp_5 allow;                 expect bw-35 provia refuse
expect super8 provia allow;              expect super8 portra_400 refuse
expect disposable-flash portra_400 allow; expect disposable-flash hp_5 allow
expect disposable-flash provia refuse
expect midcentury-rangefinder provia allow
expect midcentury-rangefinder portra_400 allow
expect midcentury-rangefinder hp_5 allow
expect midcentury-rangefinder px-100uv refuse
expect toy-camera-120 provia allow;      expect toy-camera-120 669 refuse
expect cinestill-night portra_400 allow; expect cinestill-night provia refuse
expect polaroid-600 px-100uv allow;      expect polaroid-600 669 refuse
expect polaroid-600 provia refuse
expect polaroid-sx70 px-100uv allow;     expect polaroid-sx70 portra_400 refuse
expect polaroid-packfilm 669 allow;      expect polaroid-packfilm px-100uv refuse
expect autochrome provia refuse;         expect autochrome portra_400 refuse
expect autochrome hp_5 refuse
expect technicolor-3strip provia refuse; expect technicolor-3strip portra_400 refuse
expect polavision provia refuse;         expect polavision px-100uv refuse
# neopan is B+W despite its shelf: bw camera takes it, negative camera refuses
expect bw-35 neopan_1600 allow
expect 35mm-negative neopan_1600 refuse

# print stocks: never valid as camera film, always valid as --print
expect 35mm-negative synthetic_cine_print refuse
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film \
    --print synthetic_cine_print -o "$OUT" >/dev/null 2>&1 || {
    echo "test_compat: --print resolution broken" >&2; exit 1; }

# name resolution: a complete stock name wins over stems that contain it
expect 35mm-slide kodachrome_64 allow      # suffix tie-break, not ambiguous
expect 35mm-slide kodachrome refuse        # genuinely ambiguous still refuses

# escape hatch must stay open
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-slide -f px-100uv \
    --any-film -o "$OUT" >/dev/null 2>&1 || {
    echo "test_compat: --any-film override broken" >&2; exit 1; }
# recipes: a saved recipe and the JPEG that carries it both replay to
# the same bytes; a flag after --recipe overrides its line
R1="$LIB/r1.jpg"; R2="$LIB/r2.jpg"; R3="$LIB/r3.jpg"; R4="$LIB/r4.jpg"
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative -f portra_400 \
    --ev -0.5 --halation 1.5 --grain silver --edge 1 --filter orange --seed 7 \
    -o "$R1" --save-recipe "$LIB/look.recipe" >/dev/null 2>&1 || {
    echo "test_compat: render with --save-recipe failed"; exit 1; }
grep -q '^film=kodak_portra_400$' "$LIB/look.recipe" || {
    echo "test_compat: recipe does not record the resolved film stem"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" --recipe "$LIB/look.recipe" -o "$R2" >/dev/null 2>&1
cmp -s "$R1" "$R2" || { echo "test_compat: replay from .recipe differs"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" --recipe "$R1" -o "$R3" >/dev/null 2>&1
cmp -s "$R1" "$R3" || { echo "test_compat: replay from the JPEG differs"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" --recipe "$LIB/look.recipe" --ev 0 -o "$R4" >/dev/null 2>&1
cmp -s "$R1" "$R4" && { echo "test_compat: flag after --recipe did not override"; exit 1; }
./polajuice inspect "$R1" | grep -q '^halation=1.5$' || {
    echo "test_compat: inspect does not show the embedded recipe"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" --recipe "$IN" -o "$R4" >/dev/null 2>&1 && {
    echo "test_compat: a recipe-less file was accepted as a recipe"; exit 1; }
echo "recipes: save, replay from file and JPEG byte-identical, flags override, inspect shows"

rm -f "$IN" "$OUT"
echo "compatibility matrix tests passed (37 cells + override)"
