# Preset sources and rationale

The values in `src/core/presets.c` are conservative engineering defaults, not
secret manufacturer formulas. Manufacturers normally publish qualitative
behavior and sensitometric curves, not Lightroom-style slider settings. Where
color accuracy matters, Polajuice accepts standard `.cube` 3D LUTs rather than
pretending that a 3x3 matrix is a complete stock emulation.

## Common implementation practice

- G'MIC's Color Presets and Simulate Film filters provide more than 1,100 color
  CLUTs and downloadable HaldCLUT/`.cube` files. Its instant-consumer collection
  includes Polaroid Px-680, Px-70, Px-100UV and warm/cold/push-pull variants:
  <https://gmic.eu/color_presets/>
  <https://gmic.eu/color_presets/instant_consumer_sample_1.html>
- RawTherapee documents HaldCLUT film simulation as the mechanism for matching
  global tonal and color changes to reference looks:
  <https://rawpedia.rawtherapee.com/Film_Simulation>
- darktable applies film grain to the L channel in display-referred Lab and
  exposes coarseness, strength and midtone bias. Polajuice 0.1.1 therefore uses
  one luminance noise field and a midtone-bias parameter, not independent RGB
  noise:
  <https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/grain/>

## Polaroid-style instant print

Changes from 0.1.0:

- Grain amplitude: `0.028` -> `0.0015`.
- Halation: `0.09` -> `0.006`.
- Saturation: `0.86` -> `1.04` plus channel gamma, cross-channel matrix and
  split shadow/highlight color.
- Tone response now uses deeper shadows, stronger contrast and a warmer creamy
  highlight tint.
- The frame now follows the published 600/i-Type dimensions instead of an
  approximate border.

Rationale:

- Polaroid's official dimensions are 78.94 x 76.80 mm for the image and
  88.47 x 107.52 mm overall. The engine uses these ratios rather than the common
  rounded "79 mm square in an 88 x 107 mm sheet" description:
  <https://support.polaroid.com/hc/en-us/articles/115012363647-What-are-Polaroid-photo-dimensions>
- A measured Color 600 review describes the film as virtually grainless but
  softer than conventional film, with roughly 3.5 stops of measured latitude:
  <https://www.analog.cafe/r/polaroid-600-colour-film-review-kxll>
- A laboratory/camera retailer's review characterizes Color 600 as having rich
  color, higher contrast, deeper blocked shadows and warm creamy highlights:
  <https://bluemooncameracodex.com/film-fridays/ffpolaroid600>

For established color processing, use the G'MIC `Polaroid Px-680` or
`Polaroid Px-680 Warm` `.cube` with `--lut`. The built-in scalar grade is a
restrained fallback, not a substitute for the CLUT.

## Consumer color negative

Kodak describes Gold 200 as warm and saturated while retaining fine grain and
high sharpness. The preset consequently uses low grain (`0.006`), minimal
softness, gentle saturation and a small warm highlight bias:
<https://kodakprofessional.com/sites/default/files/wysiwyg/E7022-1.pdf>

## Slide and Super 8

Kodak describes Ektachrome E100/100D as extremely fine grained, exceptionally
sharp, neutral in gray and skin reproduction, with only moderately enhanced
saturation. Both presets now have much lower grain, halation and saturation
than version 0.1.0:

- <https://www.kodak.com/en/still-film/product/professional/ektachrome-e100-film/>
- <https://www.kodak.com/content/products-brochures/Film/Super-8-EKTACHROME-100D-Color-Reversal-Film-Technical-data.pdf>

The Super 8 preset retains slightly more visible grain and softness than the
35 mm slide preset because enlargement and projection make the small frame more
apparent; this is a format/rendering adjustment rather than a claim that 100D
itself is coarse-grained.

## Black and white documentary

Ilford describes HP5 Plus as ISO 400, fine grained, medium contrast and wide in
exposure latitude. The preset now uses medium rather than hard contrast and
reduces grain from `0.060` to `0.010`:
<https://www.ilfordphoto.com/hp5-plus-35mm>

## Disposable and toy cameras

These are primarily camera/lens models, not claims about a particular emulsion.
Their distinctive parameters are optical softness, vignette and flash/corner
behavior. Grain and color remain restrained so the lens model does not become
an excuse for indiscriminate noise. The Lomo LC-A description supports punchy
contrast, saturation and shadowed vignettes as recognizable toy-camera traits:
<https://shop.lomography.com/us/lomo-lc-a-120-film-camera>

Version 0.1.2 makes direct flash a lighting stage rather than a small global
white-balance adjustment. The preset uses `-0.60 EV` ambient, a `+1.00 EV`
center-weighted beam and `0.18` neutral fill. These are conservative rendering
defaults, not camera calibration constants. The structure is grounded in:

- the inverse-square principle: doubling flash distance reduces illumination
  by four (two stops):
  <https://westcottu.com/mastering-flash-photography-5-lighting-principles-for-beginners>
- the practical consequence that direct flash exposes correctly at only one
  subject distance, producing strong foreground/background separation:
  <https://www.scantips.com/lights/flashbasics.html>
- the familiar disposable-camera outcome of harsh shadows and blown highlights
  at close range:
  <https://www.digitalcameraworld.com/cameras/compact-cameras/flashback-one35-v2-review-this-retro-disposable-camera-dupe-is-so-good-i-didnt-miss-my-iphone-snapshots>

With no depth map, a 2D center falloff is necessarily a proxy. It is intentionally
documented as such rather than presented as a physical reconstruction.

## What remains uncalibrated

The included scalar recipes have not yet been fitted from paired ColorChecker
captures. A defensible stock calibration requires the same chart and scenes
photographed digitally and on the target stock, controlled development and
scanning, then a held-out validation set. Until that exists, the project labels
its built-in values as sourced approximations and supports established external
CLUTs for serious color matching.

## CineStill-style night stock (0.2.0)

CineStill 800T is Kodak Vision3 500T with the remjet anti-halation backing
removed, which is precisely why its halos are so pronounced; halation
physics and its red-orange color are described in the Dehancer halation
article and similar colorist references. The preset therefore carries the
engine's strongest halation (0.18, radius 9), cool tungsten balance and
teal-shadow/warm-highlight split toning as a scalar approximation. For
color accuracy pair it with a CineStill 800T CLUT (e.g. PictureFX).

## Age axis (0.7.0; introduced as the expired-film preset in 0.2.0)

Storage degradation of C-41 negatives commonly presents as raised base fog,
lowered contrast, desaturation and a color drift often toward magenta as the
green-sensitive layer degrades fastest. Age is a process axis, not a camera:
`--age 0..1` applies fog, contrast decay, desaturation and a slight warm
drift display-referred after the color stage, so aging composes with any
camera and any film LUT. Amounts are conservative rendering defaults, not
measurements of a specific storage history.

## Instant expansion (0.8.0)

polaroid-sx70 shares the 600's integral geometry but models the earlier
chemistry and slower lens: softer, warmer, lower contrast; canonical film
polaroid_px-70. polaroid-packfilm models peel-apart Land-camera prints:
3 1/4 x 4 1/4 in outer (83 x 108 mm) with a roughly even thin border and a
72.9 x 95.2 mm image area, punchier contrast; canonical film polaroid_669.
Geometry uses the engine's per-preset frame millimeter fields; dimensions
are nominal print sizes, not measurements of specific film batches.

## 1920s-1950s color (0.8.0)

autochrome: the Lumiere Autochrome process (1907-1930s) formed color
additively through dyed potato-starch grains, giving a pastel palette and
visibly *colored* pointillist grain; early plates without anti-halation
backing also halate. Modeled with the grain_chroma axis (independent
per-channel noise), heavy soft-plate optics, 4:3 crop and reduced
saturation. midcentury-rangefinder: late-1930s-40s folding/rangefinder
35mm with uncoated optics (veiling flare modeled as lifted blacks and
reduced contrast); canonical film kodak_kodachrome_64.
technicolor-3strip: the beam-splitter camera and dye-transfer printing,
1.375:1 Academy gate, high saturation with controlled skin tones and
near-zero grain. All three are stylizations built from process
descriptions, not colorimetric measurements.

## Development axis (1.0.0)

Push/pull processing changes development time to trade exposure for
contrast and grain; the model raises display-referred contrast around the
mid, lifts saturation slightly, and multiplies grain amplitude with push.
Cross-processing (E-6 slide stock developed in C-41) is modeled as crossed
per-channel curves - green gaining in highlights, blue sinking in shadows -
with raised contrast and saturation, matching the commonly described lomo
signature. Both are stylizations of the process, not sensitometric
measurements, and both are refused for instant cameras, where development
is sealed inside the film unit. The measured alternative also exists: the
film catalog's Cross-processed family carries actual xpro CLUTs.
