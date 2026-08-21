#!/bin/sh
# build_web.sh - compile the core to WebAssembly with wasi-sdk.
# Downloads wasi-sdk (~110 MB, from GitHub releases) into third_party/ on
# first run; no system installation, delete the directory to uninstall.
# Output: web/polajuice.wasm - the page's engine (see web/engine.js).
set -eu
cd "$(dirname "$0")/.."

WASI_VERSION=25
WASI_DIR="third_party/wasi-sdk-$WASI_VERSION.0-x86_64-linux"
if [ ! -x "$WASI_DIR/bin/clang" ]; then
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64) ASSET="wasi-sdk-$WASI_VERSION.0-x86_64-linux.tar.gz" ;;
        Darwin-arm64) ASSET="wasi-sdk-$WASI_VERSION.0-arm64-macos.tar.gz"
                      WASI_DIR="third_party/wasi-sdk-$WASI_VERSION.0-arm64-macos" ;;
        *) echo "unsupported host; get wasi-sdk manually into third_party/" >&2; exit 1 ;;
    esac
    echo "== fetching wasi-sdk $WASI_VERSION ($ASSET)"
    curl -fSL -o third_party/wasi-sdk.tar.gz \
        "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$WASI_VERSION/$ASSET"
    tar xzf third_party/wasi-sdk.tar.gz -C third_party
    rm third_party/wasi-sdk.tar.gz
fi

echo "== $($WASI_DIR/bin/clang --version | head -1)"
"$WASI_DIR/bin/clang" --target=wasm32-wasi -O3 -std=c17 -Iinclude -Ithird_party \
    src/core/image.c src/core/image_io.c src/core/lut3d.c src/core/presets.c \
    src/core/pipeline.c src/core/stb_impl.c src/web/pj_wasi_main.c \
    -o web/polajuice.wasm -lm \
    -Wl,-z,stack-size=1048576 \
    -Wl,--initial-memory=67108864 \
    -Wl,--max-memory=2147483648
ls -la web/polajuice.wasm
