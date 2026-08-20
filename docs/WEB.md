# Polajuice in the browser

`make` builds the native binary; `make wasm` compiles the identical core to
WebAssembly with wasi-sdk (auto-downloaded to `third_party/` on first run;
~110 MB, no system install, delete the directory to uninstall). Release
tarballs ship `web/polajuice.wasm` prebuilt and pre-verified, so building
it is only needed after source changes.

The wasm is a WASI command binary: the page runs it through the vendored
`@bjorn3/browser_wasi_shim` (MIT/Apache-2.0, `web/vendor/`), giving it an
in-memory filesystem per render. Because the module has no JS-generated
glue, the exact artifact that ships is testable headlessly: `make
check-wasm` runs it under node through `web/engine.js` - the page's own
loader - and byte-compares a render against the native CLI. Browser and
CLI produce identical files for identical inputs and seed.

## Local test and deploy

```sh
make fetch-luts                  # once, if the film library is absent
sh scripts/stage_web_films.sh    # canonical cubes -> web/films/ + manifest
make serve                       # http://localhost:8000
```

A local server is required even for testing (browsers refuse module and
wasm loads over file://). Deployment is part of `scripts/release.sh`,
which stages films, snapshots `web/` onto the `gh-pages` branch via a
temporary worktree (main is never checked out from under you), and
force-pushes that branch only - gh-pages is a build artifact, replaced
wholesale each release, while main stays sources-only. One-time GitHub
step: Settings -> Pages -> deploy from branch `gh-pages`, folder `/`.

## Memory and performance

An 8-12 MP render peaks around 400-600 MB of linear-light float buffers;
the module declares 64 MB initial / 2 GB maximum memory, within wasm32
browser limits. The preview/full-size split in the page keeps the UI
responsive; `-msimd128` and a worker thread are the standard next steps
if speed ever matters.
