# Polajuice 1.6.0 — the juice package

One engine, two front-ends: **polajuice** for stills, **superjuice** for
movies. Both are thin drivers over the same core (`libpolajuice.a`) and
the same four-axis model — camera, film, developing, age — so a look
tuned on a photograph renders identically on every frame of a film, and
`make` builds both binaries from one tree.

Polajuice is a small, modern C library and command-line program for turning
clean digital photographs into modeled photographic processes. It treats a
look as an ordered pipeline—camera, emulsion, development, print/scan and
age—rather than as a bag of unrelated filters.

The engine reads and writes JPEG, PNG and PPM directly; the format is chosen
from the file extension, JPEG loading honors the EXIF orientation tag, and the
only build requirements remain a C17 compiler and `libm` (JPEG/PNG codecs are
the vendored public-domain stb single-header libraries in `third_party/`).
The processing core works in linear floating-point RGB and includes film-like
tone response, color matrices, optical softness, vignette, halation,
luminance-dependent grain, square crops, exact-proportion instant-print
framing, an age stage and standard `.cube` 3D color LUTs. HEIC, TIFF, ICC and
RAW remain future work (libvips and Little CMS territory).

## Build

## superjuice: movies

One command, movie in, vintage movie out:

```sh
./superjuice clip.mp4 -c super8 --age 0.3
# -> clip_super8.mp4, audio passed through untouched
```

superjuice drives FFmpeg itself (decoding, H.264 encoding at `--crf 18`
by default, audio passthrough); it needs `ffmpeg` on PATH and tells you
so if it is missing. All the stills options apply: `-f` for the film,
`--develop`, `--age`, `--strength`, `--seed`, `-o` for an explicit
output path. For custom pipelines (different codecs, filters, no
re-encode of your choosing), plumbing mode remains: with no input file,
superjuice reads YUV4MPEG2 on stdin and writes it on stdout, and the
engine itself stays dependency-free:

```sh
ffmpeg -v error -i clip.mp4 -f yuv4mpegpipe -pix_fmt yuv420p - \
  | ./superjuice -c super8 --seed 7 \
  | ffmpeg -v error -f yuv4mpegpipe -i - -i clip.mp4 \
           -map 0:v -map 1:a? -c:v libx264 -crf 18 -c:a copy vintage.mp4
```

The temporal axis is what makes footage read as film rather than a
filtered video: **frame-decorrelated grain** (real grain "boils" — every
frame's crystals are new, which falls out of the engine's seeded grain
as `seed ⊕ frame`), **gate weave** (the frame drifts subpixel in the
transport gate), and **exposure flicker** (uneven camera speed). Weave
and flicker are per-camera character — super8 carries the most,
autochrome flickers like the hand-cranked process it was,
technicolor-3strip runs steady as the studio machine it was — and every
camera gets boiling grain. All options match polajuice (`-f`,
`--develop`, `--age`, `--strength`, compatibility rules included);
instant cameras are refused, since a Polaroid is a still print. Stills
rendering is untouched: the temporal stages only run in movie mode, and
polajuice output is bit-identical to 1.1.0. Rough throughput is around
a second per 1080p frame — preview at proxy resolution, render final
overnight, as film labs always did.

Building requires only a C17 compiler and `libm`:

```sh
make          # native binaries: polajuice and superjuice
make check
make wasm     # the same engine for the browser (Emscripten; see docs/WEB.md)
```

The web build renders entirely client-side — drop a photo on the page,
pick a camera and film, download the result; pixels are byte-identical to
the CLI for the same inputs and seed.

## Use

List the nine built-in cameras and their canonical films:

```sh
./polajuice cameras
```

A look is a **camera** plus a **film**. The camera (`-c`) is the format and
process archetype: optics, grain, flash, framing, age. The film (`-f`) is the
color chemistry, a `.cube` 3D LUT named by stock and found automatically in
the film library (`data/luts/`, populated by `make fetch-luts`; override the
location with `$POLAJUICE_FILMS`). Every camera has a canonical film applied
by default when installed, so the everyday command is just:

```sh
./polajuice apply IMG_2041.jpg -c polaroid-600
# -> IMG_2041_polaroid-600.jpg, graded through polaroid_px-680.cube
```

Choose a different stock by name (case-insensitive substring; exact stem
wins; ambiguity lists candidates) or by path, list what is installed with
`polajuice films [filter]`, or force the built-in scalar color engine:

```sh
./polajuice apply IMG_2041.jpg -c 35mm-negative -f portra_800
./polajuice apply IMG_2041.jpg -c bw-35 -f ./my_custom.cube
./polajuice apply IMG_2041.jpg -c polaroid-600 --no-film
```

The output format follows the output extension; when `--output` is omitted
the result lands next to the input as `<input>_<camera>.<ext>` in the
input's own format. The 0.3.x flags `-p/--preset/--lut` remain as quiet
aliases.

Apply a commonly used film-emulation LUT for the stock tone/color transform
while retaining Polajuice's preset-specific lighting, input balance, optical,
grain and framing stages. Supplying a LUT replaces the entire built-in stock
stage so its tone curve is not applied twice; it no longer suppresses direct
flash or other camera stages:

```sh
./polajuice apply IMG_2041.jpg -c polaroid-600 -f px-70 -o polaroid.jpg
```

G'MIC publishes downloadable `.cube` versions of its established film CLUTs,
including Polaroid Px-680, Px-70 and warm/cold variants:

<https://gmic.eu/color_presets/instant_consumer_sample_1.html>

Download `.cube` packs for all preset families into `data/luts/`
(G'MIC publishes them in `.cube` form directly); add `--rawtherapee` to
`scripts/fetch_luts.sh` to also fetch and convert the 402 MB RawTherapee
HaldCLUT collection via `tools/haldclut2cube.py`:

```sh
make fetch-luts
```

Partial-strength render:

```sh
./polajuice apply IMG_2041.jpg -c polaroid-600 --strength 0.75
```

Sample photos ship in `web/samples/` (six of the author's own, CC BY 4.0):
the web page shows them as a click-to-load strip with a suggested camera
per photo, and CLI users can run e.g.
`./polajuice apply web/samples/classic-jaguar.jpg -c technicolor-3strip`.

HEIC still needs one external conversion (`magick input.heic input.jpg`, or
`sips -s format jpeg` on macOS). All files are treated as 8-bit sRGB at the
boundary and converted to linear RGB for processing.

## Presets

Presets are camera/format/process archetypes; film-stock color belongs in a
3D LUT paired with the matching archetype (see `data/presets/README.md` for
the pairing table).

- `35mm-negative` - clean 35mm color negative (was `consumer-35-warm`)
- `35mm-slide` - 35mm reversal slide (was `slide-classic`)
- `polaroid-600` - integral instant print with frame (was `polaroid-classic`)
- `super8` - 1.36:1 projector-aperture gate, coarse grain, halation (was `super8-classic`)
- `disposable-flash` - direct on-camera flash, dark falloff, soft lens
- `bw-35` - 35mm ISO 400 black-and-white (was `bw-documentary`)
- `toy-camera-120` - square, heavy vignette, plastic-lens softness
- `cinestill-night` - remjet-stripped cine stock rig, strong red halation
- `polaroid-sx70` - the earlier integral Polaroid: warmer, dreamier, slower
- `polaroid-packfilm` - peel-apart Land-camera prints, thin even border
- `midcentury-rangefinder` - late-30s/40s 35mm, uncoated-lens flare,
  canonical Kodachrome 64
- `technicolor-3strip` - the beam-splitter cinema camera: Academy gate,
  saturated dye-transfer color, near-zero grain
- `autochrome` - Lumiere plates, 1907-1930s: pastel palette, soft plate
  optics and pointillist *colored* grain (per-channel noise, the engine's
  `grain_chroma` axis)

## Architecture: the darkroom model

A render decomposes the way a film workflow does:

    image -> F (film color transform, a measured .cube LUT)
          -> P (optional print/scan stock, a second chained LUT)
          -> develop (push/pull/cross chemistry)
          -> A (age: per-process differential dye fade)
          -> S (spatial effects: grain, halation, softness, vignette,
                flash, frames - always the camera's, never baked into cubes)

`--print NAME|FILE` chains a print emulsion after the film - the cinema
negative->print model (Vision3 through Kodak 2383 territory); print
stocks live in their own catalog category and their own selector on the
web, never mixed into the film list. Three clearly-labeled `synthetic_*`
placeholder prints ship in `data/prints/` (generated stylizations, not
measurements - the honest kind of battery included); measured 2383-class
cubes supersede them the moment they land in the library. LUT sampling uses tetrahedral
interpolation (the industry standard; better neutral-axis and hue
behavior than trilinear at the same cost).

Aging is per-process differential dye fade rather than a uniform wash:
slide films drift red-magenta as the cyan dye dies first, C-41 scans go
cool, instant prints amber, and black-and-white silver does not fade at
all - it only fogs, with a faint warm stain. The film's process (or the
camera's, when it implies exactly one) selects the profile, so the same
age slider tells a different, chemically plausible story on every
stock. Rates are stylizations of commonly described fade behavior, not
densitometry; sourcing honesty per look is tracked in
docs/PRESET_SOURCES.md, including which cubes are measured-from-film
versus inspired-by.

## Camera and film compatibility

Not every camera takes every film, and Polajuice enforces the physics
rather than letting nonsense pairings render silently. Three tiers:

**Sealed processes take no film at all.** For `autochrome` (Lumiere
plates) and `technicolor-3strip` (dye-transfer cinema), the process *is*
the medium - "Provia in an Autochrome" describes nothing that could
exist. The CLI refuses a `-f` for them with an explanation, and the web
page disables the film selector.

**Every other camera declares which film processes it accepts**, and only
matching films are offered or allowed:

| camera | takes |
|---|---|
| 35mm-negative, cinestill-night | color negative (C-41) |
| 35mm-slide, super8 | slide (E-6/K-14) |
| bw-35 | black-and-white |
| disposable-flash | color negative, black-and-white |
| midcentury-rangefinder, toy-camera-120 | slide, negative, black-and-white |
| polaroid-600, polaroid-sx70 | Polaroid integral only |
| polaroid-packfilm | peel-apart pack film only |
| autochrome, technicolor-3strip | none (sealed) |

The film's process is inferred from its place in the film library
(colorslide is slide, negative_old/new are C-41, and so on, with
stem-based overrides for instant stocks); a cube supplied by path from
outside the library has an unknowable process and is never blocked. On
the web, the film dropdown is rebuilt per camera to show only compatible
families - and enforcement is engine-deep, not just interface-deep: the
WebAssembly engine performs the same compatibility check itself, so the
artifact refuses nonsense pairings even when asked directly, exactly as
the CLI binary does. The full camera x family matrix is frozen as a
test that runs on every build; `polajuice cameras`, `describe` and the help panel all print a
"films:" line so nothing is left to guessing.

**Deliberate mismatches remain available, because darkrooms allowed
them.** `--any-film` bypasses every check - lomo culture is built on
wrong film in wrong places, and the engine should not be more dogmatic
than a lab. The one historically famous mismatch has first-class
support instead: slide stock through negative chemistry is exactly what
`--develop cross` emulates, and the film catalog's Cross-processed
family carries measured CLUTs of the real thing. Anachronisms (Portra
800 in the midcentury-rangefinder) pass freely: the film loads, and
history's blushes are not the engine's business.

One classification note for honesty's sake: `cinestill-night` is
physically a film trick (Vision3 cine stock with its anti-halation layer
removed) shot in an ordinary 35mm body; it lives in the camera list
because halation is an optical rendering trait, and it accepts C-41
negative films accordingly.

Development is its own axis too: `--develop` takes `normal`, `push+1`,
`push+2`, `pull-1` or `cross` (E-6 slide through C-41 chemistry). Push
raises contrast and coarsens grain, pull relaxes both, and cross gives
the shifted-color lomo signature; all apply after the color stage so
they compose with any film LUT, and all are refused with a note for
instant cameras, whose development happens inside the film unit. The
web page groups the film library into historical families (Kodachrome,
Ektachrome, the Portra generations, the Ilford speed ladder, the
Polaroid chemistries and more) with process labels and notes.

Aging is not a camera: `--age 0..1` (a slider on the web) is a third axis
alongside camera and film - base fog with magenta drift, contrast decay
and desaturation that composes with any camera and any film, applied
after the color stage so it survives LUTs. `polajuice describe CAMERA`
and `polajuice cameras` print trait summaries generated from each
preset's own parameters, so the help always matches what renders.

The scalar defaults are conservative structural recipes grounded in the sources
listed in `docs/PRESET_SOURCES.md`; they are not presented as manufacturer
calibration data. Exact color emulation should use a reference 3D LUT. This is
the standard approach used by G'MIC and RawTherapee because a small collection
of saturation and white-balance sliders cannot describe film color adequately.

## Design principles

- Deterministic output for a given seed, independent of traversal order.
- Luminance-only, midtone-biased grain rather than colored pixel noise.
- Direct-flash lighting is separate from film-stock color.
- Processing in the domain appropriate to the effect.
- A small public C API with no backend types exposed.
- Scalar reference implementations before SIMD optimization.
- Presets composed from camera, stock, processing, output and age components.
- No global mutable state.

See `docs/ARCHITECTURE.md`, `docs/PRESET_SOURCES.md` and `docs/ROADMAP.md`.

## License

GNU General Public License version 3 or later. See `COPYING`.
