# Polajuice 0.8.0

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

Building requires only a C17 compiler and `libm`:

```sh
make          # native binary
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
