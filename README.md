# Polajuice 0.2.0

Polajuice is a small, modern C library and command-line program for turning
clean digital photographs into modeled photographic processes. It treats a
look as an ordered pipeline—camera, emulsion, development, print/scan and
age—rather than as a bag of unrelated filters.

This first working prototype deliberately uses dependency-free PPM I/O so the
core can be built and tested anywhere. The processing engine works
in linear floating-point RGB and includes film-like tone response, color
matrices, optical softness, vignette, halation, luminance-dependent grain,
square crops, exact-proportion instant-print framing and standard `.cube` 3D
color LUTs. JPEG, TIFF, PNG, HEIC, ICC and RAW are
the next I/O layer, intended to be implemented through libvips and Little CMS.

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

Render an image:

```sh
./polajuice apply input.ppm \
    --preset 35mm-negative \
    --seed 42 \
    --output result.ppm
```

Apply a commonly used film-emulation LUT for the stock tone/color transform
while retaining Polajuice's preset-specific lighting, input balance, optical,
grain and framing stages. Supplying a LUT replaces the entire built-in stock
stage so its tone curve is not applied twice; it no longer suppresses direct
flash or other camera stages:

```sh
./polajuice apply input.ppm \
    --preset polaroid-600 \
    --lut polaroid_px-680.cube \
    --output result.ppm
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
./polajuice apply input.ppm -p polaroid-600 \
    --strength 0.75 -o result.ppm
```

For ordinary images during this PPM-only stage, ImageMagick or GIMP can perform
the temporary conversion:

```sh
magick input.heic -auto-orient input.ppm
./polajuice apply input.ppm -p 35mm-slide -o output.ppm
magick output.ppm output.jpg
```

PPM files are treated as sRGB at the boundary and converted to linear RGB for
processing.

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
