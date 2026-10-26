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
# 1.21: the format crop and extended development ride in the recipe
R5="$LIB/r5.jpg"; R6="$LIB/r6.jpg"
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c super8 --no-film --crop full \
    --develop push+3.5 -o "$R5" --save-recipe "$LIB/look2.recipe" >/dev/null 2>&1 || {
    echo "test_compat: --crop full / --develop push+3.5 refused"; exit 1; }
grep -q '^crop=full$' "$LIB/look2.recipe" || { echo "test_compat: recipe lacks crop=full"; exit 1; }
grep -q '^develop=push+3.5$' "$LIB/look2.recipe" || { echo "test_compat: recipe lacks develop=push+3.5"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" --recipe "$R5" -o "$R6" >/dev/null 2>&1
cmp -s "$R5" "$R6" || { echo "test_compat: crop/develop replay differs"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c super8 --no-film --develop push+5 -o "$R6" >/dev/null 2>&1 && {
    echo "test_compat: push+5 was accepted"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c super8 --no-film --crop sideways -o "$R6" >/dev/null 2>&1 && {
    echo "test_compat: crop=sideways was accepted"; exit 1; }
echo "recipes: crop=full and develop=push+3.5 round-trip; out-of-range values refused"
# 1.24: scene adjustments ride in the recipe and the JPEG, flags on the CLI
R7="$LIB/r7.jpg"; R8="$LIB/r8.jpg"
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --temp 0.4 --tint -0.2 \
    --black 0.01 --shadows 0.5 --contrast 0.25 --grad 1.5 --gradangle 180 --gradpos 0.1 \
    -o "$R7" --save-recipe "$LIB/look3.recipe" >/dev/null 2>&1 || { echo "test_compat: scene flags refused"; exit 1; }
for k in 'temp=0.4' 'tint=-0.2' 'black=0.01' 'shadows=0.5' 'contrast=0.25' 'grad=1.5' 'gradangle=180' 'gradpos=0.1' 'highlights=0'; do
    grep -q "^$k\$" "$LIB/look3.recipe" || { echo "test_compat: recipe lacks $k"; exit 1; }
done
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" --recipe "$R7" -o "$R8" >/dev/null 2>&1
cmp -s "$R7" "$R8" || { echo "test_compat: scene replay from the JPEG differs"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --grad 4 -o "$R8" >/dev/null 2>&1 && {
    echo "test_compat: grad=4 was accepted"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film -o "$R8" >/dev/null 2>&1
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --temp 0 --grad 0 --contrast 0 -o "$R7" >/dev/null 2>&1
cmp -s "$R7" "$R8" || { echo "test_compat: zero scene adjustments changed the render"; exit 1; }
echo "recipes: scene adjustments round-trip through file and JPEG; zero is identity; ranges enforced"
# 1.25: lens and framing keys, and the geometry line names the trim and the straightening
R9="$LIB/r9.jpg"; R10="$LIB/r10.jpg"
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --rotate 2.5 --distortion 0.05 \
    --devignette 0.5 --denoise 1 --trimx 0.1 --trimy 0.1 --trimw 0.8 --trimh 0.8 -o "$R9" >/dev/null 2>&1 || {
    echo "test_compat: lens flags refused"; exit 1; }
./polajuice inspect "$R9" | grep -q '^rotate=2.5$' || { echo "test_compat: recipe lacks rotate"; exit 1; }
./polajuice inspect "$R9" | grep -q 'trim=[0-9]*,[0-9]* rot=2.5' || { echo "test_compat: geometry line lacks trim/rot"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" --recipe "$R9" -o "$R10" >/dev/null 2>&1
cmp -s "$R9" "$R10" || { echo "test_compat: lens replay from the JPEG differs"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --rotate 30 -o "$R10" >/dev/null 2>&1 && {
    echo "test_compat: rotate=30 was accepted"; exit 1; }
echo "recipes: lens and framing keys round-trip; geometry carries trim and rotation"

rm -f "$IN" "$OUT"
echo "compatibility matrix tests passed (37 cells + override)"
