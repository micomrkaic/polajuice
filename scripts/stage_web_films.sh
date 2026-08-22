#!/bin/sh
# stage_web_films.sh - copy the curated, historically organized film catalog
# from the film library (data/luts, populated by 'make fetch-luts') into
# web/films/ and write the families manifest the web UI reads.
#
# Stocks are grouped by family and development process; stems must match
# cubes in the library, and missing ones are skipped with a warning (pack
# names occasionally drift between G'MIC releases). Extra stems can be
# appended as arguments; they land in an "Extras" family.

set -eu
cd "$(dirname "$0")/.."

LIB="${LUT_DIR:-data/luts}"
DEST="web/films"

[ -d "$LIB" ] || {
    echo "no film library at $LIB; run 'make fetch-luts' first" >&2
    exit 1
}

CATALOG='Kodachrome|K-14|The archive of the 20th century, 1935-2009: restrained saturation, deep reds, unmatched dark stability|kodak_kodachrome_25 kodak_kodachrome_64 kodak_kodachrome_64_generic kodak_kodachrome_200
Ektachrome and Elite|E-6|Kodak slide line: cleaner blues, punchier than Kodachrome, home-developable|kodak_e-100_gx_ektachrome_100 kodak_ektachrome_100_vs kodak_elite_chrome_200 kodak_elite_chrome_400 kodak_elite_extracolor_100
Fuji slide|E-6|Velvia for saturated landscapes, Provia for accuracy, Astia for skin|fuji_velvia_50 fuji_velvia_100_generic fuji_provia_100f fuji_provia_400x fuji_astia_100f fuji_sensia_100 agfa_precisa_100
Kodak Portra|C-41|The portrait negatives, three generations: NC/VC era through the unified modern line|kodak_portra_160 kodak_portra_400 kodak_portra_800 kodak_portra_160_nc kodak_portra_160_vc kodak_portra_400_nc kodak_portra_400_uc kodak_portra_400_vc
Fuji negative|C-41|Consumer Superia line and the pro 160C/400H/800Z: cooler greens than Kodak|fuji_superia_100 fuji_superia_400 fuji_superia_800 fuji_superia_1600 fuji_160c fuji_400h fuji_800z
Kodak black-and-white|B+W|Tri-X, the century of photojournalism; T-Max, the cleaner modern school|kodak_tri-x_400 kodak_t-max_100 kodak_t-max_400 kodak_t-max_3200
Ilford black-and-white|B+W|Pan F to Delta 3200: the full speed ladder, plus C-41-process XP2|ilford_pan_f_plus_50 ilford_fp_4_plus_125 ilford_hp_5_plus_400 ilford_delta_100 ilford_delta_400 ilford_delta_3200 ilford_xp_2
Agfa and Rollei|B+W|Continental character: APX classics, Retro contrast, blue-blind Ortho|agfa_apx_25 agfa_apx_100 rollei_retro_80s rollei_ortho_25
Polaroid integral|internal|SX-70 through 600-era chemistry, plus expired Time-Zero|polaroid_px-70 polaroid_px-680 polaroid_px-680_warm polaroid_px-100uv+_warm polaroid_time_zero_expired
Polaroid pack film|internal|Peel-apart era: Land-camera 66x and Fuji FP stocks|polaroid_665 polaroid_669 polaroid_690 fuji_fp-100c fuji_fp_100c fuji_fp-3000b
Cross-processed|E-6 in C-41|Slide film through the wrong chemistry, as measured|fuji_superia_200_xpro lomography_x-pro_slide_200
Print stocks|print|Cinema print emulsions, chained after the negative|kodak_2383 kodak_2393 fuji_3513'

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
        fuji_velvia_50) echo "The landscape legend: exaggerated greens and reds" ;;
        ilford_hp_5_plus_400) echo "Classic ISO 400 black-and-white: medium contrast, honest tonality" ;;
        kodak_tri-x_400) echo "The photojournalism standard for half a century" ;;
        lomography_x-pro_slide_200) echo "Cross-processed slide look: shifted colors, heavy contrast" ;;
        *) echo "" ;;
    esac
}

mkdir -p "$DEST"
STATE="$DEST/.stage_state"
: > "$STATE.families"
printf '0' > "$STATE.total"

echo "$CATALOG" | while IFS='|' read -r name process note stems; do
    [ -n "$name" ] || continue
    fam_json=""
    for stem in $stems; do
        found=$(find "$LIB" -name "$stem.cube" -print -quit)
        if [ -z "$found" ]; then
            echo "!! not in library, skipping: $stem" >&2
            continue
        fi
        cp "$found" "$DEST/$stem.cube"
        printf '%d' "$(( $(cat "$STATE.total") + 1 ))" > "$STATE.total"
        entry=$(printf '{"stem":"%s","blurb":"%s"}' "$stem" "$(blurb_for "$stem")")
        fam_json="$fam_json${fam_json:+,}$entry"
    done
    [ -n "$fam_json" ] || continue
    prev=$(cat "$STATE.families")
    printf '%s%s{"name":"%s","process":"%s","note":"%s","stocks":[%s]}' \
        "$prev" "${prev:+,}" "$name" "$process" "$note" "$fam_json" \
        > "$STATE.families"
done

families=$(cat "$STATE.families")
total=$(cat "$STATE.total")
rm -f "$STATE.families" "$STATE.total"

if [ "$#" -gt 0 ]; then
    extra=""
    for stem in "$@"; do
        found=$(find "$LIB" -name "$stem.cube" -print -quit)
        [ -z "$found" ] && { echo "!! not in library: $stem" >&2; continue; }
        cp "$found" "$DEST/$stem.cube"
        total=$((total + 1))
        extra="$extra${extra:+,}$(printf '{"stem":"%s","blurb":""}' "$stem")"
    done
    [ -n "$extra" ] && families="$families${families:+,}{\"name\":\"Extras\",\"process\":\"\",\"note\":\"\",\"stocks\":[$extra]}"
fi

printf '{"families":[%s]}\n' "$families" > "$DEST/films.json"
echo "staged $total stocks"
echo "manifest: $DEST/films.json"
