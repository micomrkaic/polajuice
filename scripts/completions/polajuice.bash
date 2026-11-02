# bash completion for polajuice and superjuice. Cameras and films come
# from the binaries themselves, so the lists follow the install and the
# film library. Installed by 'make install' to
# $(PREFIX)/share/bash-completion/completions/; source it by hand from a
# checkout: . scripts/completions/polajuice.bash

_polajuice_cameras() { polajuice cameras 2>/dev/null | awk '/^[a-z0-9]/ {print $1}'; }
_polajuice_films() { polajuice films 2>/dev/null | awk 'NF == 1 {print $1}'; }
_polajuice_scene_flags() {
    echo "--temp --tint --black --shadows --highlights --contrast --grad --gradangle --gradpos \
--radial --radialx --radialy --radialsize --denoise --sharpen --recover --distortion --devignette \
--rotate --keystonev --keystoneh --trimx --trimy --trimw --trimh"
}

_polajuice() {
    local cur prev words cword
    _init_completion 2>/dev/null || { COMPREPLY=(); cur=${COMP_WORDS[COMP_CWORD]}; prev=${COMP_WORDS[COMP_CWORD-1]}; words=("${COMP_WORDS[@]}"); cword=$COMP_CWORD; }
    local sub=${words[1]}
    if [ "$cword" -eq 1 ]; then
        COMPREPLY=($(compgen -W "apply develop serve cameras describe films inspect paths --help" -- "$cur"))
        return
    fi
    case "$prev" in
        -c|--camera) COMPREPLY=($(compgen -W "$(_polajuice_cameras)" -- "$cur")); return ;;
        -f|--film|--print) COMPREPLY=($(compgen -W "$(_polajuice_films)" -- "$cur")); _filedir cube 2>/dev/null; return ;;
        --develop) COMPREPLY=($(compgen -W "normal push+1 push+2 push+3 push+4 push+0.5 push+1.5 pull-1 pull-2 pull-3 cross cross+1 cross+2" -- "$cur")); return ;;
        --crop) COMPREPLY=($(compgen -W "camera full" -- "$cur")); return ;;
        --grain) COMPREPLY=($(compgen -W "classic silver" -- "$cur")); return ;;
        --filter) COMPREPLY=($(compgen -W "yellow orange red green blue" -- "$cur")); return ;;
        --lens) COMPREPLY=($(compgen -W "auto none" -- "$cur")); return ;;
        -o|--output|--recipe|--save-recipe) _filedir 2>/dev/null; return ;;
        --port) return ;;
        --web) _filedir -d 2>/dev/null; return ;;
    esac
    case "$sub" in
        apply)
            if [[ "$cur" == -* ]]; then
                COMPREPLY=($(compgen -W "-c --camera -f --film --no-film --any-film --print -o --output --strength --develop --crop \
--age --grain --edge --seed --ev --halation --filter --lens --linear --recipe --save-recipe $(_polajuice_scene_flags)" -- "$cur"))
            else _filedir '@(jpg|jpeg|JPG|JPEG|png|PNG|ppm|nef|NEF|dng|DNG|tif|tiff)' 2>/dev/null; fi ;;
        develop)
            if [[ "$cur" == -* ]]; then COMPREPLY=($(compgen -W "-o --output --ev --linear" -- "$cur"))
            else _filedir '@(nef|NEF|dng|DNG|png|tif|tiff)' 2>/dev/null; fi ;;
        serve)
            if [[ "$cur" == -* ]]; then COMPREPLY=($(compgen -W "--port --web" -- "$cur"))
            else _filedir -d 2>/dev/null; fi ;;
        describe) COMPREPLY=($(compgen -W "$(_polajuice_cameras)" -- "$cur")) ;;
        films) COMPREPLY=($(compgen -W "$(_polajuice_films)" -- "$cur")) ;;
        inspect) _filedir 2>/dev/null ;;
    esac
}
complete -F _polajuice polajuice

_superjuice() {
    local cur prev words cword
    _init_completion 2>/dev/null || { COMPREPLY=(); cur=${COMP_WORDS[COMP_CWORD]}; prev=${COMP_WORDS[COMP_CWORD-1]}; }
    case "$prev" in
        -c|--camera) COMPREPLY=($(compgen -W "$(_polajuice_cameras | grep -v polaroid)" -- "$cur")); return ;;
        -f|--film) COMPREPLY=($(compgen -W "$(_polajuice_films)" -- "$cur")); return ;;
        --develop) COMPREPLY=($(compgen -W "normal push+1 push+2 push+3 push+4 pull-1 pull-2 pull-3 cross cross+1" -- "$cur")); return ;;
        --crop) COMPREPLY=($(compgen -W "camera full" -- "$cur")); return ;;
        --grain) COMPREPLY=($(compgen -W "classic silver" -- "$cur")); return ;;
        --filter) COMPREPLY=($(compgen -W "yellow orange red green blue" -- "$cur")); return ;;
        -o|--output|--recipe) _filedir 2>/dev/null; return ;;
    esac
    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "-c --camera -f --film --no-film --any-film --develop --crop --age --strength --grain --edge \
--ev --halation --recipe --seed --motion-scale --filter -o --output --crf $(_polajuice_scene_flags)" -- "$cur"))
    else _filedir '@(mp4|MP4|mov|MOV|mkv|avi|m4v)' 2>/dev/null; fi
}
complete -F _superjuice superjuice
