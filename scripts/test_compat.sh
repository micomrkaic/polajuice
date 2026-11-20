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

# 1.27: keystone, sharpen, radial, recover ride in the recipe and replay;
# cross on a B&W camera is dropped with a note and renders as normal;
# --lens auto on a non-raw says so and leaves the corrections alone
R11="$LIB/r11.jpg"; R12="$LIB/r12.jpg"
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --keystonev 0.3 --keystoneh -0.2 \
    --sharpen 1 --radial 0.5 --radialx 0.1 --radialy -0.1 --radialsize 0.4 --recover 1 -o "$R11" >/dev/null 2>&1 || {
    echo "test_compat: 1.27 keys refused"; exit 1; }
for k in 'keystonev=0.3' 'keystoneh=-0.2' 'sharpen=1' 'radial=0.5' 'radialx=0.1' 'radialsize=0.4' 'recover=1'; do
    ./polajuice inspect "$R11" | grep -q "^$k\$" || { echo "test_compat: recipe lacks $k"; exit 1; }
done
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" --recipe "$R11" -o "$R12" >/dev/null 2>&1
cmp -s "$R11" "$R12" || { echo "test_compat: 1.27 recipe replay differs"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --keystonev 2 -o "$R12" >/dev/null 2>&1 && {
    echo "test_compat: keystonev=2 was accepted"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c bw-35 --no-film --develop cross -o "$R11" 2>"$LIB/note.txt" >/dev/null || {
    echo "test_compat: cross on bw-35 failed outright"; exit 1; }
grep -q "black and white" "$LIB/note.txt" || { echo "test_compat: no note for cross on bw-35"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c bw-35 --no-film -o "$R12" >/dev/null 2>&1
./polajuice inspect "$R11" | grep -q '^develop=cross$' || { echo "test_compat: cross not recorded"; exit 1; }
python3 - "$R11" "$R12" <<'PY' || { echo "test_compat: cross on bw-35 changed the pixels"; exit 1; }
import sys
a, b = [open(p, 'rb').read() for p in sys.argv[1:3]]
# the JPEG comment differs (develop=cross vs normal); the scan data must not
def scan(d): return d[d.rfind(b'\xff\xda'):]
sys.exit(0 if scan(a) == scan(b) else 1)
PY
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --lens auto -o "$R11" 2>"$LIB/note.txt" >/dev/null || {
    echo "test_compat: --lens auto on a JPEG failed"; exit 1; }
grep -q "needs a raw" "$LIB/note.txt" || { echo "test_compat: --lens auto on a JPEG gave no note"; exit 1; }
./polajuice inspect "$R11" | grep -q '^distortion=0$' || { echo "test_compat: --lens auto touched a JPEG's corrections"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --lens sideways -o "$R11" >/dev/null 2>&1 && {
    echo "test_compat: --lens sideways was accepted"; exit 1; }
echo "recipes: keystone, sharpen, radial, recover round-trip; cross dropped on B&W; --lens auto needs a raw"

# HEIC: refused with the ways in when no converter is on the PATH; with
# one (a stand-in here that writes a known JPEG) the file is converted
# beside the source to NAME_from_heic.jpg, reused on the next run, and
# the render equals the JPEG's. A HEIC renamed .jpg is told by content.
HEIC="$LIB/phone.heic"
printf '\000\000\000\030ftypheic\000\000\000\000mif1heic' > "$HEIC"
FAKEBIN="$LIB/fakebin"; mkdir -p "$FAKEBIN"
./polajuice apply "$IN" -c 35mm-negative --no-film --strength 0 -o "$LIB/asjpeg.jpg" >/dev/null 2>&1
printf '#!/bin/sh\ncp "%s" "$4"\n' "$LIB/asjpeg.jpg" > "$FAKEBIN/heif-convert"; chmod +x "$FAKEBIN/heif-convert"
PATH="/nonexistent" ./polajuice apply "$HEIC" -c super8 --no-film -o "$R11" 2>"$LIB/note.txt" >/dev/null && {
    echo "test_compat: a HEIC with no converter was accepted"; exit 1; }
grep -q "HEIC" "$LIB/note.txt" || { echo "test_compat: HEIC refusal does not say HEIC:"; cat "$LIB/note.txt"; exit 1; }
PATH="$FAKEBIN:$PATH" POLAJUICE_FILMS="$LIB" ./polajuice apply "$HEIC" -c super8 --no-film --seed 3 -o "$R11" 2>"$LIB/note.txt" >/dev/null || {
    echo "test_compat: HEIC through the converter failed:"; cat "$LIB/note.txt"; exit 1; }
grep -q "converted with heif-convert" "$LIB/note.txt" || { echo "test_compat: no conversion note"; exit 1; }
[ -s "$LIB/phone_from_heic.jpg" ] || { echo "test_compat: no derived JPEG beside the HEIC"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$LIB/asjpeg.jpg" -c super8 --no-film --seed 3 -o "$R12" >/dev/null 2>&1
python3 - "$R11" "$R12" <<'PY' || { echo "test_compat: HEIC render differs from the JPEG's"; exit 1; }
import sys
a, b = [open(p, 'rb').read() for p in sys.argv[1:3]]
def scan(d): return d[d.rfind(b'\xff\xda'):]
sys.exit(0 if scan(a) == scan(b) else 1)
PY
rm -f "$FAKEBIN/heif-convert"
PATH="$FAKEBIN:/nonexistent" ./polajuice inspect "$HEIC" 2>/dev/null | grep -q "^width:" || { echo "test_compat: the derived JPEG was not reused without a converter"; exit 1; }
cp "$HEIC" "$LIB/renamed.jpg"
PATH="/nonexistent" ./polajuice inspect "$LIB/renamed.jpg" 2>"$LIB/note.txt" >/dev/null && { echo "test_compat: a HEIC named .jpg was accepted"; exit 1; }
grep -q "HEIC" "$LIB/note.txt" || { echo "test_compat: renamed HEIC not told by content"; exit 1; }
# 1.29: a look exported as a 3D LUT and as a darktable style. The cube
# has its header and n^3 rows; the style names four modules for Super 8
# (lowpass, lut3d, vignette, grain) with parameter blobs of darktable
# 4.6's struct sizes and the cube's path inside the lut3d blob; --install
# puts the files under darktable's lut root and styles folder, and says
# what to set when darktable has no lut root; a recipe (a rendered JPEG)
# exports like flags do.
DT="$LIB/dt"; mkdir -p "$DT/out" "$DT/cfg" "$DT/luts"
POLAJUICE_FILMS="$LIB" ./polajuice lut -c 35mm-negative --no-film --temp 0.5 -o "$DT/out/neg.cube" --size 9 >/dev/null || { echo "test_compat: lut export failed"; exit 1; }
[ "$(grep -c '^[0-9]' "$DT/out/neg.cube")" = 729 ] || { echo "test_compat: cube does not hold 9^3 rows"; exit 1; }
grep -q '^LUT_3D_SIZE 9$' "$DT/out/neg.cube" || { echo "test_compat: cube header"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice lut -c 35mm-negative --no-film -o "$DT/out/plain.cube" --size 9 >/dev/null
cmp -s "$DT/out/neg.cube" "$DT/out/plain.cube" && { echo "test_compat: --temp did not reach the cube"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice lut -c 35mm-negative --no-film --size 9 >/dev/null 2>&1 && { echo "test_compat: lut without -o accepted"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice darktable -c super8 --no-film -o "$DT/out/reel" --size 9 >/dev/null || { echo "test_compat: darktable export failed"; exit 1; }
[ -s "$DT/out/reel.cube" ] && [ -s "$DT/out/reel.dtstyle" ] || { echo "test_compat: darktable export wrote no files"; exit 1; }
python3 - "$DT/out/reel.dtstyle" <<'PY' || { echo "test_compat: style structure wrong"; exit 1; }
import sys, struct, xml.etree.ElementTree as ET
t = ET.parse(sys.argv[1]).getroot()
assert t.tag == "darktable_style" and t.find("info/name").text == "reel"
ops = {p.find("operation").text: (int(p.find("module").text), bytes.fromhex(p.find("op_params").text)) for p in t.findall("style/plugin")}
assert list(ops) == ["lowpass", "lut3d", "vignette", "grain"], list(ops)
sizes = {"lowpass": (4, 28), "lut3d": (3, 512 + 12 + 2048 * 6 + 128), "vignette": (4, 44), "grain": (2, 16)}
for op, (ver, size) in sizes.items():
    assert ops[op][0] == ver and len(ops[op][1]) == size, (op, ops[op][0], len(ops[op][1]))
lut = ops["lut3d"][1]
assert lut[:512].rstrip(b"\0") == b"reel.cube" and struct.unpack("<3i", lut[512:524]) == (0, 0, 0)
radius = struct.unpack("<f", ops["lowpass"][1][4:8])[0]
assert abs(radius - 0.1874 * 6000 / 145) < 0.01, radius
channel, scale, strength, bias = struct.unpack("<i3f", ops["grain"][1])
assert channel == 2 and 0 < strength <= 100 and 0 < scale <= 6400 / 213.2 + 0.01 and 0 <= bias <= 100
assert struct.unpack("<f", ops["vignette"][1][8:12])[0] < 0      # darkens
assert "Not carried: halation" in t.find("info/description").text and "camera=super8" in t.find("info/description").text
PY
POLAJUICE_FILMS="$LIB" ./polajuice darktable -c bw-35 --no-film -o "$DT/out/bw" --size 9 >/dev/null
grep -q '<operation>lowpass</operation>' "$DT/out/bw.dtstyle" && { echo "test_compat: lowpass in a style for a camera without a stated resolution"; exit 1; }
POLAJUICE_DARKTABLE_CONFIG="$DT/cfg" ./polajuice darktable -c super8 --no-film -o reel --install --size 9 >/dev/null 2>"$LIB/note.txt" && { echo "test_compat: --install without a lut root accepted"; exit 1; }
grep -q "3D lut root folder" "$LIB/note.txt" || { echo "test_compat: no guidance for a missing lut root"; exit 1; }
printf 'something=1\nplugins/darkroom/lut3d/def_path=%s\n' "$DT/luts" > "$DT/cfg/darktablerc"
POLAJUICE_DARKTABLE_CONFIG="$DT/cfg" ./polajuice darktable -c super8 --no-film -o reel --install --size 9 >/dev/null || { echo "test_compat: --install failed"; exit 1; }
[ -s "$DT/luts/polajuice/reel.cube" ] && [ -s "$DT/cfg/styles/reel.dtstyle" ] || { echo "test_compat: --install put nothing in place"; exit 1; }
grep -q "$(printf 'polajuice/reel.cube' | od -An -tx1 | tr -d ' \n')" "$DT/cfg/styles/reel.dtstyle" || { echo "test_compat: installed style does not point at polajuice/reel.cube"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative --no-film --temp 0.5 -o "$R11" >/dev/null 2>&1
POLAJUICE_FILMS="$LIB" ./polajuice lut --recipe "$R11" -o "$DT/out/fromjpeg.cube" --size 9 >/dev/null || { echo "test_compat: lut from a rendered JPEG failed"; exit 1; }
cmp -s "$DT/out/fromjpeg.cube" "$DT/out/neg.cube" || {
    # the titles differ by name; the lattice must not
    [ "$(grep '^[0-9]' "$DT/out/fromjpeg.cube" | cksum)" = "$(grep '^[0-9]' "$DT/out/neg.cube" | cksum)" ] || { echo "test_compat: cube from the recipe differs from the flags'"; exit 1; } ; }
# a stock shelved twice under one name (Tri-X in the black-and-white
# pack and in a color-negative pack): the camera decides which file
mkdir -p "$LIB/negative_old"
cp "$LIB/bw/ilford_hp_5_plus_400.cube" "$LIB/bw/kodak_tri-x_400.cube"
cp "$LIB/negative_new/kodak_portra_400.cube" "$LIB/negative_old/kodak_tri-x_400.cube"
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c bw-35 -f kodak_tri-x_400 -o "$R11" >/dev/null 2>"$LIB/note.txt" || { echo "test_compat: bw-35 refused the twice-shelved Tri-X:"; cat "$LIB/note.txt"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice apply "$IN" -c 35mm-negative -f kodak_tri-x_400 -o "$R12" >/dev/null 2>"$LIB/note.txt" || { echo "test_compat: 35mm-negative refused the twice-shelved Tri-X:"; cat "$LIB/note.txt"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice curves -c bw-35 -f kodak_tri-x_400 --vs ilford_hp_5_plus_400 --table >/dev/null 2>&1 || { echo "test_compat: curves on the twice-shelved Tri-X failed"; exit 1; }
rm -f "$LIB/bw/kodak_tri-x_400.cube" "$LIB/negative_old/kodak_tri-x_400.cube"
echo "library: a stock shelved twice under one name resolves to the file the camera takes"

# The README's annex states which controls are the same control; the
# exact identities are pinned here. (1) a yellow filter is ev, temp and
# tint together; (2) shadows is contrast from middle gray to four stops
# under it; (3) the camera's crop is a centered trim with the full frame.
mkdir -p "$LIB/ident"
python3 - "$LIB/ident" <<'PY' || { echo "test_compat: a control identity stated in the README's annex no longer holds"; exit 1; }
import subprocess, sys, math
d = sys.argv[1]
def cube(*args, n=9):
    subprocess.run(["./polajuice", "lut", "-c", "35mm-negative", "--no-film", "-o", d + "/t.cube", "--size", str(n), *args], check=True, capture_output=True)
    return [[float(x) for x in l.split()] for l in open(d + "/t.cube") if l[0].isdigit()]
def worst(a, b, keep=None):
    return max(abs(x - y) for i, (p, q) in enumerate(zip(a, b)) if keep is None or keep[i] for x, y in zip(p, q)) * 255
t = (1.0, 0.95, 0.35)
L = 0.2126 * t[0] + 0.7152 * t[1] + 0.0722 * t[2]
a, b, c = (math.log2(v / L) for v in t)
ev = (a + b + c) / 3; temp = a - c; tint = 2 * (ev - b)
assert abs(temp) <= 2 and abs(tint) <= 1
assert worst(cube("--filter", "yellow"), cube("--ev", f"{ev:.6f}", "--temp", f"{temp:.6f}", "--tint", f"{tint:.6f}")) < 0.05
n = 17
dec = lambda v: v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
lat = [(r / (n - 1), g / (n - 1), bl / (n - 1)) for bl in range(n) for g in range(n) for r in range(n)]
Y = [0.2126 * dec(p[0]) + 0.7152 * dec(p[1]) + 0.0722 * dec(p[2]) for p in lat]
under = [0.18 / 16 * 1.02 < y < 0.18 * 0.98 for y in Y]
s = -0.5
assert worst(cube("--shadows", str(s), n=n), cube("--contrast", f"{2 * math.log2(1 - s / 4):.6f}", n=n), under) < 0.05
cc = 0.5
assert worst(cube("--contrast", str(cc), "--shadows", f"{4 * (2 ** (cc / 2) - 1):.6f}", n=n), cube(n=n), under) < 0.05
PY
python3 - "$LIB/ident/field.ppm" <<'PY'
import sys
# 640x480 of a deterministic pattern: Super 8's gate takes 640x470, whole-pixel margins
W, H = 640, 480
body = bytes((x * 7 + y * 13 + c * 29) % 256 for y in range(H) for x in range(W) for c in range(3))
open(sys.argv[1], "wb").write(b"P6\n%d %d\n255\n" % (W, H) + body)
PY
POLAJUICE_FILMS="$LIB" ./polajuice apply "$LIB/ident/field.ppm" -c super8 --no-film --seed 3 -o "$LIB/ident/cam.ppm" >/dev/null 2>&1
python3 - "$LIB/ident/field.ppm" "$LIB/ident" <<'PY' || { echo "test_compat: the camera's crop is no longer a centered trim"; exit 1; }
import subprocess, sys
def size(p):
    f = open(p, "rb"); assert f.readline().strip() == b"P6"
    l = f.readline()
    while l.startswith(b"#"): l = f.readline()
    return tuple(int(v) for v in l.split())
W, H = size(sys.argv[1]); w, h = size(sys.argv[2] + "/cam.ppm")
tw, th = w / W, h / H
subprocess.run(["./polajuice", "apply", sys.argv[1], "-c", "super8", "--no-film", "--seed", "3", "--crop", "full",
                "--trimx", f"{(1 - tw) / 2:.6f}", "--trimy", f"{(1 - th) / 2:.6f}", "--trimw", f"{tw:.6f}", "--trimh", f"{th:.6f}",
                "-o", sys.argv[2] + "/trim.ppm"], check=True, capture_output=True)
a, b = open(sys.argv[2] + "/cam.ppm", "rb").read(), open(sys.argv[2] + "/trim.ppm", "rb").read()
sys.exit(0 if a == b else 1)
PY
echo "identities: yellow filter = ev+temp+tint; shadows = contrast under gray; camera crop = centered trim"

# 1.30: the look's color charted. The SVG is well-formed with its four
# panels; the table has 24 patches; an identity look (strength 0) moves
# nothing; --vs adds the second look's columns; -o or --table is required.
POLAJUICE_FILMS="$LIB" ./polajuice curves -c 35mm-slide --no-film -o "$DT/out/slide.svg" --table > "$DT/out/table.txt" || { echo "test_compat: curves failed"; exit 1; }
python3 - "$DT/out/slide.svg" "$DT/out/table.txt" <<'PY' || { echo "test_compat: curves output wrong"; exit 1; }
import sys, xml.etree.ElementTree as ET
t = ET.parse(sys.argv[1]).getroot()
ns = "{http://www.w3.org/2000/svg}"
titles = [e.text for e in t.iter(ns + "text") if e.text and e.text[:2] in ("1.", "2.", "3.", "4.")]
assert len(titles) == 4, titles           # panels 5 and 6 are checked by name below
assert len(list(t.iter(ns + "polyline"))) >= 9          # R, G, B; hue turn, chroma scale; eye, sensitivity; shift, purity
lines = open(sys.argv[2]).read().splitlines()
first = [i for i, l in enumerate(lines) if l.startswith("dark skin")][0]
rows = lines[first:first + 24]
assert len(rows) == 24 and rows[0].startswith("dark skin") and rows[23].startswith("black")
red = [l for l in rows if l.startswith("red ")][0].split()
assert float(red[2].rstrip("%")) > 20        # a slide stock saturates a red
PY
POLAJUICE_FILMS="$LIB" ./polajuice curves -c 35mm-slide --no-film --strength 0 --table > "$DT/out/ident.txt" || { echo "test_compat: curves --table alone failed"; exit 1; }
python3 - "$DT/out/ident.txt" <<'PY' || { echo "test_compat: an identity look moves the patches"; exit 1; }
import sys
for l in open(sys.argv[1]).read().splitlines()[2:26]:
    nums = [float(x.rstrip("%\u00b0")) for x in l.split()[-3:] if x != "-"]
    assert all(abs(v) < 1.5 for v in nums), l
PY
# by wavelength: the identity has the eye's own sensitivity, moves no hue
# and keeps the purity; a red filter on a B&W camera weighs red up and
# blue-green down; the chart gains its two wavelength panels
python3 - "$DT/out/ident.txt" <<'PY' || { echo "test_compat: identity is not the eye's sensitivity by wavelength"; exit 1; }
import sys
rows = [l.split() for l in open(sys.argv[1]).read().splitlines() if l.strip().endswith(("x1.00", "-")) and " nm " in l and l.lstrip()[0].isdigit()]
assert len(rows) == 14, len(rows)
for r in rows:
    assert abs(float(r[2])) < 0.03, r
    if r[-1] != "-": assert r[-1] == "x1.00" and abs(float(r[6])) <= 1, r
PY
POLAJUICE_FILMS="$LIB" ./polajuice curves -c bw-35 --no-film --filter red --table > "$DT/out/redfilter.txt"
python3 - "$DT/out/redfilter.txt" <<'PY' || { echo "test_compat: a red filter does not weigh red up and blue-green down"; exit 1; }
import sys
v = {l.split()[0]: l.split()[2] for l in open(sys.argv[1]).read().splitlines() if " nm " in l and l.lstrip()[0].isdigit()}
assert float(v["640"]) > 0.5 and float(v["480"]) < -1.0 and abs(float(v["560"])) < 0.2, v
PY
grep -q '5. Spectral sensitivity' "$DT/out/slide.svg" && grep -q '6. By wavelength' "$DT/out/slide.svg" || { echo "test_compat: chart lacks the wavelength panels"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice curves -c 35mm-negative --no-film --vs ilford_hp_5_plus_400 --table 2>/dev/null | head -1 | grep -q '|' || { echo "test_compat: --vs added no second look"; exit 1; }
POLAJUICE_FILMS="$LIB" ./polajuice curves -c 35mm-slide --no-film >/dev/null 2>&1 && { echo "test_compat: curves with neither -o nor --table accepted"; exit 1; }
echo "curves: the look charted as SVG and tabulated, by patch and by wavelength; identity moves nothing; --vs overlays a second film"

echo "export: look as a .cube and as a darktable style (lut3d, grain, vignette, lowpass); --install; from a recipe"

echo "HEIC: refused with the ways in; converted beside the source when a converter is on the PATH; told by content"

rm -f "$IN" "$OUT"
echo "compatibility matrix tests passed (37 cells + override)"
