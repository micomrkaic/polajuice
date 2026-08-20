# Polajuice in the browser

`make` builds the native binary; `make wasm` builds the same core for
WebAssembly. The pipeline, the stb codecs and the EXIF orientation parser
all compile unchanged, so a given input, camera, film, strength and seed
renders byte-identical pixels in the browser and the CLI. Photos never
leave the machine: decoding and rendering happen locally in the page.

## Building

One-time Emscripten SDK setup:

```sh
git clone https://github.com/emscripten-core/emsdk
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```

Then, from the repo:

```sh
make wasm                        # -> web/polajuice.js + web/polajuice.wasm
make fetch-luts                  # if the film library is not yet installed
sh scripts/stage_web_films.sh    # canonical cubes -> web/films/ + manifest
make serve                       # http://localhost:8000
```

A local server is required even for testing: browsers refuse to load
`.wasm` over `file://`. Extra stocks can be staged by name:
`sh scripts/stage_web_films.sh kodak_portra_800 fuji_velvia_50`.

## How the page works

- The engine exposes camera enumeration, so the dropdown and each camera's
  canonical film come from the same preset table the CLI uses.
- The uploaded file's bytes are written to Emscripten's in-memory
  filesystem as `/in.jpg` or `/in.png` (chosen from the MIME type) so
  format dispatch and JPEG EXIF orientation work exactly as on the
  desktop; the chosen film is written to `/film.cube`.
- "Render preview" renders at a 1400 px long edge (a box-average
  downscale in linear light; the pipeline is resolution-aware, so the
  preview is a faithful miniature). "Render full size" renders at native
  resolution and downloads `<name>_<camera>.jpg`.
- Films are fetched lazily from `films/` per the `films.json` manifest and
  cached; "canonical (auto)" mirrors the CLI's default-film behavior,
  including the scalar-engine fallback.

## Deploying to GitHub Pages

The `web/` directory is self-contained. Simplest path: keep built
artifacts off `main` and publish them from a `gh-pages` branch —

```sh
make wasm && sh scripts/stage_web_films.sh
git checkout --orphan gh-pages
git rm -rf --cached . >/dev/null
git add -f web && git commit -m "web build"
git subtree push --prefix web origin gh-pages   # or push and set Pages to /web
git checkout main
```

then in the repository settings enable Pages from the `gh-pages` branch.
(Any equivalent flow works; the only rule kept from the repo's history is
that generated artifacts stay out of `main`.)

## Performance notes

The engine has been verified as WebAssembly outside the browser (clang
wasm32-wasi + node WASI): renders are byte-identical to the native binary
for the same input, camera, film, strength and seed.

Memory: an 8-12 MP render peaks at roughly 400-600 MB of linear-light
float buffers, hence the 256 MB initial / 2 GB maximum in build_web.sh;
wasm32 in browsers allows up to 4 GB, so full-resolution phone photos
fit, but the growth headroom is deliberate, not decorative. The heavy
stages are the box blurs (halation, softness) and per-pixel color math;
if speed ever matters, `-msimd128` and a worker thread are the standard
next steps, and the preview/full-size split already keeps the UI
responsive.
