#!/bin/sh
# build_web.sh - compile the polajuice core + web shim to WebAssembly.
#
# Requires the Emscripten SDK on PATH (https://emscripten.org):
#   git clone https://github.com/emscripten-core/emsdk
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source ./emsdk_env.sh
#
# Output: web/polajuice.js + web/polajuice.wasm. Serve the web/ directory
# (make serve) or deploy it to GitHub Pages; browsers refuse to load .wasm
# from file:// so a local server is required even for testing.

set -eu
cd "$(dirname "$0")/.."

command -v emcc >/dev/null || {
    echo "emcc not found; install and activate the Emscripten SDK first" >&2
    exit 1
}

EXPORTED='_pj_web_render,_pj_web_status,_pj_web_version,_pj_web_camera_count,_pj_web_camera_name,_pj_web_camera_description,_pj_web_camera_film,_malloc,_free'
RUNTIME='FS,cwrap,UTF8ToString'

echo "== emcc $(emcc --version | head -1)"
emcc -O3 -std=c17 -Iinclude -Ithird_party \
    src/core/image.c src/core/image_io.c src/core/lut3d.c \
    src/core/presets.c src/core/pipeline.c src/core/stb_impl.c \
    src/web/pj_web.c \
    -o web/polajuice.js \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createPolajuice \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS="$EXPORTED" \
    -sEXPORTED_RUNTIME_METHODS="$RUNTIME" \
    -sSTACK_SIZE=1048576 \
    -sINITIAL_MEMORY=268435456 \
    -sMAXIMUM_MEMORY=2147483648

ls -la web/polajuice.js web/polajuice.wasm
echo
echo "built. next steps:"
echo "  sh scripts/stage_web_films.sh   # copy canonical films into web/films/"
echo "  make serve                      # local test at http://localhost:8000"
