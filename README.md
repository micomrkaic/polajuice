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

A taxonomy note on movie cameras, because honesty is house policy.
Three tiers: **motion-native** (super8, technicolor-3strip - movie
formats, full stop), **motion-plausible** (the 35mm family - 35mm is
the cinema gauge, and cinestill-night is literally motion-picture
stock), and **motion-counterfactual** (toy-camera-120, disposable-flash,
midcentury-rangefinder, autochrome - cameras that never advanced a
frame in history). superjuice refuses only the instant still cameras, where the output
medium cannot do motion: a Polaroid print is constitutionally a single
frame. History supplies exactly one exception, and it is in the camera
list: **polavision**, Polaroid's 1977 instant movie system - additive-
color phototape in a sealed cassette, self-developed in ninety seconds,
projected dim through its own player, off the market by 1979. Dim,
muted, softly flickering, with Autochrome-descended colored grain: Land's
magnificent folly, and the one true instant movie camera. Counterfactuals roll freely with a one-line disclosure -
"never shot motion; treating it as an imaginary movie camera" - because
they violate history, not physics, and imagining the movie camera
Lumiere never built from his own Autochrome plates is half the fun.

One command, movie in, vintage movie out:

```sh
./superjuice clip.mp4 -c super8 --age 0.3
# -> clip_super8.mp4, audio passed through untouched
```

superjuice drives FFmpeg itself (decoding, H.264 encoding at `--crf 18`
by default, audio passthrough); it needs `ffmpeg` on PATH and tells you
so if it is missing. All the stills options apply: `-f` for the film,
`--develop`, `--crop`, `--age`, `--grain`, `--edge`, `--strength`,
`--seed`, `-o` for an explicit output path. For custom pipelines (different codecs, filters, no
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

### One command to 1962: scripts/super8ify.sh

```sh
scripts/super8ify.sh IMG_5048.MOV
# -> IMG_5048_super8.mp4: 18 fps, 720p, Kodachrome 64, aged, with
#    magnetic-stripe sound if audiotard is installed
```

The script drives the whole reel: video through superjuice (frame rate,
proxy scale, gate, grain, weave), sound through
[audiotard](https://github.com/micomrkaic/audiotard) - the same
dependency-free C17 philosophy applied to audio: tape wow and flutter,
hiss, a small tube amp, projector-speaker bandwidth. Every choice is an
environment knob (CAMERA, FILM, AGE, SEED, FPS, HEIGHT, WOW, FLUTTER,
SILENT=yes for period-correct silence); without audiotard, set
SILENT=yes or let a soundless input produce a silent reel naturally.
Super 8 was silent for most people anyway - the stripe is the luxury
option, then as now.

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
`sips -s format jpeg` on macOS). JPEG, PNG and PPM are treated as 8-bit
sRGB at the boundary and converted to linear RGB for processing; they
render exactly as they did before raw input existed.

### Raw input: the scene-linear door

`.nef` (Nikon) and `.dng` (Adobe, iPhone, most converters) open through
a second door that decodes the sensor data itself: dependency-free
TIFF walking, lossless JPEG and Nikon's Huffman scheme, black and white
levels, the as-shot white balance, the camera's color matrix (from the
DNG, or a small per-model table for NEF) and a bilinear demosaic. The
D610 path is verified against LibRaw over a whole frame (mean
difference 2e-5 in linear sRGB); other Nikon bodies use the same
dcraw matrices, unverified here.

```sh
./polajuice apply DSC_0412.NEF -c 35mm-negative -f portra_400
./polajuice apply IMG_0033.dng -c super8 --ev -0.5 --halation 1.5
./polajuice develop DSC_0412.NEF            # the straight print, no look
./polajuice inspect DSC_0412.NEF            # clipped fraction, mean level
./polajuice apply scan.tif --linear -c bw-35 # dcraw -4 -T, darktable linear
```

What changes with raw is where the highlights are. An 8-bit file has
already been through a tone curve and clipped at white, so halation and
the film curve work on a plateau. Raw comes in scene-linear with the
sensor's white at 1.0; the pipeline opens it one stop up, so that white
sits a stop above display white, and rolls that top stop in with a
shoulder after halation has seen it and before the film transforms,
which are display-referred. `--ev` moves from there. The practical
advice stands: expose a stop under in the camera and let the shoulder
bring the highlights back.

`--linear` takes a 16-bit PNG or uncompressed RGB TIFF as scene-linear
with no gamma decode, for exports that are already linear. A 16-bit file
from an ordinary converter is not that: it is display-referred with
better quantization, and goes through the normal door.

`--halation 0..2` scales the camera's glow on any input; it is at its
strongest from raw, where speculars still exist above white. Flash
cameras (the instant presets) clip at the flash stage before halation,
so they see less of the headroom.

Sensors read out masked columns and rows around the picture (the D610
delivers 6080x4028 for a 6016x4016 image), and the door cuts to the
image area before anything sees the samples: DNG's ActiveArea and
DefaultCrop tags, Nikon's CropHiSpeed in the MakerNote, and for the
bodies that write that tag without a crop (the D610 among them) a
table of image areas with the masked side read from the data (the
D610 masks 46 columns at the right, nothing else). The border never
reaches the demosaic, the statistics or the print.

The demosaic is bilinear, which fringes at hard high-contrast edges;
the camera softness stage and film grain cover most of it, and an
adaptive demosaic is on the roadmap. Clipped sensor channels are clipped
to neutral white (dcraw's default), not reconstructed.

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

    raw   -> decode (NEF/DNG: black, white, white balance, matrix,
             demosaic; scene-linear, sensor white = 1.0)
          -> +1 stop and `--ev`
          -> halation (`--halation`), on the real highlights
          -> scene shoulder (rolls the top stop in; 8-bit input skips
             this stage and renders as before)
    image -> F (film color transform, a measured .cube LUT)
          -> E (development edge effects, `--edge`: developer exhaustion
                at boundaries - border, fringe and Eberhard effects)
          -> G (silver grain, `--grain silver`: the grains that form the
                image, coarse in the shadows and fine in the highlights)
          -> P (optional print/scan stock, a second chained LUT)
          -> develop (push/pull/cross chemistry)
          -> A (age: per-process differential dye fade)
          -> S (spatial effects: classic grain, halation, softness,
                vignette, flash, frames - always the camera's, never
                baked into cubes)

Two grain models. `--grain classic` (the default, unchanged since 1.0)
lays display noise over the finished picture at the end. `--grain
silver` generates the grain inside the film: developed-fraction
statistics set the amplitude (binomial, so paper white and deep black
stay quiet), and texture follows exposure - the large fast crystals
develop first, so low exposure is carried by the coarse population
alone and higher exposure fills in with fine grain. Because it forms
before the print stock, push and age, those act on it as they act on
the image: pushing favors the coarse population, an aged print carries
fogged grain. Color stock gets three partly correlated dye-cloud layers,
the fast blue-sensitive top layer grainiest; silver stock one luminance
field. Mid gray carries the same RMS in both models for the same camera
and film, so switching changes texture, not noisiness.

`--edge 0..2` is the developer running out where density forms.
Developer exhausts locally, so the dense side of a boundary sees
fresher developer and gains density (border effect), the thin side
loses a little (fringe), and a small dense detail gains most
(Eberhard). It is a first-order steady state, `D' = D (1 - g Dbar) /
(1 - g D)` with `Dbar` the density averaged over a 0.15 mm diffusion
length on a 35mm diagonal: multiplicative in density and asymmetric,
unlike an unsharp mask, and a uniform area maps exactly to itself. 1 is
a dilute developer, 2 stand development. Silver stock works on luma and
applies one gain to all channels; color stock works per dye layer, which
is how DIR-coupler interimage effects behave. Ignored for instant
cameras.

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

Development is its own axis too: `--develop` takes `normal`, `push+N`
or `pull-N` for any number of stops from -3 to +4 (`push+1.5` is fine),
`cross` (E-6 slide through C-41 chemistry) or `cross+N` for a pushed
cross process. Push raises contrast and coarsens grain, pull relaxes
both, and cross gives the shifted-color lomo signature; all apply after
the color stage so they compose with any film LUT, and all are refused
with a note for instant cameras, whose development happens inside the
film unit. Within -1..+2 the response is linear and renders exactly as
earlier versions did; each further stop counts 0.6 of one, the way a
third and fourth stop of push buy less contrast than the first two (the
developer has little left to bring up in the shadows) while the grain
keeps coarsening.

Every camera with a format of its own - the square of a 126 cartridge,
the 1.03:1 instant print, the 1.36:1 Super 8 gate, the Academy gate -
cuts that format from the middle of the input, which loses the edges of
a 3:2 or 4:3 frame. `--crop full` keeps the whole input at its own
aspect instead; an instant camera then puts its border around the full
frame. Cameras without a format crop are the same either way, and the
choice rides in the recipe as `crop=`. The
web page groups the film library into historical families (Kodachrome,
Ektachrome, the Portra generations, the Ilford speed ladder, the
Polaroid chemistries and more) with process labels and notes.

### The connoisseur tier

The staged catalog covers the icons from the standard fetch; the deep
cuts - Ektar, Acros, Scala, the infrareds (SFX, HIE, Rollei IR),
redscale, Natura - live in the RawTherapee Film Simulation Collection,
a 402 MB archive of largely measured-from-film HaldCLUTs. One command
unlocks them:

```sh
sh scripts/fetch_luts.sh --rawtherapee   # needs python3 + Pillow
```

The catalog is self-completing: those families are declared as glob
patterns, so whatever the collection's exact stems turn out to be, the
next release stages them into their proper process families - Ektar
beside the Portras, Acros beside Neopan 1600 (which ships in the
standard packs and is staged as of this release), the infrareds in
their own family. Nothing to edit; fetch, release, they appear.

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

## The workspace and recipes

The page at <https://micomrkaic.github.io/polajuice/> is a workspace:
controls on the left grouped the way the pipeline runs (scene, camera,
film, development), a live preview that re-renders on every change,
original / A / current views with a pin for A/B comparison, a 1:1
toggle, a strip of loaded images and the samples, and the recipe.

A recipe is the whole look as `key=value` text (docs/RECIPES.md). The
engine writes it into every JPEG it saves, the page reads it back from
the render and shows it with the equivalent `polajuice apply` line, and
the CLI replays it:

```sh
polajuice apply IN.jpg --recipe look.recipe       # a saved look
polajuice apply IN.jpg --recipe render.jpg        # any polajuice JPEG
polajuice apply IN.jpg --recipe look.recipe --ev 0   # flags after it win
polajuice inspect render.jpg                      # shows what made it
```

Tune in the page, keep the look under a name, save the .recipe, run
the folder from the shell. `superjuice --recipe` and `RECIPE=` in
super8ify take the same files.

### Comparing looks: wipe, histogram, loupe, bracket

Four tools on the view, all page-side (renders are unchanged):

- **Wipe** (`w`): a divider dragged across the view, the current look
  on the right and the original - or the look pinned as A - on the
  left. The comparison image is laid on the picture, not the box: the
  engine writes where the picture landed (crop and placement) as a
  comment line after the recipe in every JPEG, so a Polaroid's border,
  a Super 8 gate or the 126 square all line up, and two cameras with
  different formats compare on the picture.
- **Histogram and clipping**: a per-channel log histogram of what is
  shown, with the share of pixels clipped at white per channel and
  crushed at black; the *clipping* button paints them on the view, red
  for white and blue for black. On the native engine a raw input also
  reports the sensor's own numbers (mean in stops below sensor white,
  share at sensor white), so print clipping and sensor clipping are
  told apart.
- **Loupe** (`l`): a 320 px window at 1:1 of the full-size render under
  the pointer, for judging grain and edge effects without rendering
  full size by hand; the full-size render is made once per look.
- **Bracket**: nine renders of the current look stepping one knob
  (exposure, develop, strength, age, halation, edge) in a grid over
  the view; click a cell to take that value. On the native engine the
  nine take about a second; in the browser engine they take their time.

### `polajuice serve`: the workspace on the native engine

The page on GitHub renders in WebAssembly, single-threaded. Served by
polajuice itself, it renders here, on every core, and sees your disk:

```sh
polajuice serve ~/Pictures/raw          # http://localhost:8000
make serve DIR=~/Pictures/raw           # the same
```

The page probes for the server on load and, finding it, sends every
render to it (the header says "native engine, N threads"). The
folder's images fill the strip (raw ones shown as straight prints),
"Export next to source" writes the full-size JPEG beside the original
with the recipe embedded, "Apply to folder" runs the current look on
every image, and kept looks are files in `looks/` under the repo, so
they travel with it. A served render is byte-identical to the CLI's,
which the test suite checks. The server binds 127.0.0.1 only; a custom
.cube loaded in the page still renders in wasm, since the server has
no file for it.

## Speed

Every image stage runs in row bands on one thread per core (POSIX
threads; `POLAJUICE_THREADS=N` overrides, 1 = serial). A band computes
exactly what the serial loop computed for those rows, in the same
order, so the output is bit-identical at any thread count - the test
suite renders at 1, 2 and 5 threads and compares bytes, and the wasm
engine, which has no threads, matches the native CLI byte for byte.
The sRGB transfer, which the pipeline crosses five or six times per
render, goes through 8192-cell interpolated tables instead of `powf`
(error under 2e-5, a hundredth of an 8-bit level; 8-bit input decodes
exactly). Against 1.17.1 a PNG render differs by at most one level on
a few thousandths of a percent of samples; JPEG output can show larger
local differences because a one-level change perturbs a DCT block.
The references in `tests/ref/` were rendered by 1.17.1 and pin this.
`POLAJUICE_PROFILE=1` prints every stage's wall time to stderr.
Raw decoding runs tile-parallel with banded conversion passes, and
JPEG output goes through polajuice's own band-parallel baseline
encoder (stb's arithmetic, so the same pixels; restart markers join
the bands, and the band height is fixed so the file does not depend
on the thread count).

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
