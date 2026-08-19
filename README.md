# Polajuice 0.3.0

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
make
make check
```

## Use

List the nine built-in recipes:

```sh
./polajuice presets
```

Render an image; the output format follows the output extension, and when
`--output` is omitted the result lands next to the input as
`<input>_<preset>.<ext>` in the input's own format:

```sh
./polajuice apply IMG_2041.jpg --preset 35mm-negative --seed 42
# -> IMG_2041_35mm-negative.jpg
```

Apply a commonly used film-emulation LUT for the stock tone/color transform
while retaining Polajuice's preset-specific lighting, input balance, optical,
grain and framing stages. Supplying a LUT replaces the entire built-in stock
stage so its tone curve is not applied twice; it no longer suppresses direct
flash or other camera stages:

```sh
./polajuice apply IMG_2041.jpg \
    --preset polaroid-600 \
    --lut data/luts/instant_consumer/polaroid_px-680.cube \
    --output polaroid.jpg
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
./polajuice apply IMG_2041.jpg -p polaroid-600 --strength 0.75
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
- `cinestill-night` - new: remjet-stripped cine stock, strong red halation
- `expired-film` - new: decades-old negative; the age stage (base fog with
  magenta drift plus contrast decay) applies even when a LUT supplies the
  color, since age is a process trait, not a stock trait

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
