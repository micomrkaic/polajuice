# Architecture

## Public boundary

`include/polajuice.h` is the stable public boundary. The initial API exposes an
opaque image object, deterministic render options, preset discovery and PPM
I/O. Backend-specific objects must not cross this boundary.

## Processing order

1. Decode, orient and color-normalize the source.
2. Simulate target-camera optics.
3. Add exposure-domain effects such as vignette and halation.
4. Apply emulsion color response and characteristic curve.
5. Add grain as a density-dependent spatial field.
6. Apply print/scanner response.
7. Add aging, physical damage and borders.
8. Convert into the output profile and encode.

The prototype implements the middle of this pipeline. PPM I/O is isolated in
`image.c`, and standard `.cube` handling is isolated in `lut3d.c`.

## Deterministic randomness

Noise is derived from a hash of seed and coordinates. It does not
consume a global PRNG stream. A future tiled and multithreaded implementation
can therefore evaluate regions in any order without changing the result.

Version 0.1.1 adds grain to luminance only and biases it toward midtones. This
follows the established darktable grain model more closely than the earlier
prototype's independent RGB noise. Spatial noise is smoothly interpolated to
avoid square grain cells.

Large-scale correlated noise should use deterministic multiresolution fields
addressed by absolute coordinates. Video should additionally include the frame
number and effect identifier in the key.

## Color

The PPM prototype converts sRGB to linear float RGB. The production backend
will normalize embedded profiles into linear Rec.2020 using Little CMS. Optical
effects run in linear light; characteristic curves are applied around a linear
middle-gray pivot; creative color grading and external LUTs operate in encoded
sRGB. The production color-managed backend will make the LUT input space an
explicit recipe property.

The `.cube` reader supports 3D tables from 2 through 128 points per axis,
`DOMAIN_MIN`, `DOMAIN_MAX`, comments and red-fastest IRIDAS/Adobe ordering. It
uses trilinear interpolation. Applying a LUT in the wrong input color space will
produce the wrong result, even if the LUT itself is valid.

As of 0.1.2, lighting and input balance are independent from stock rendering.
An external LUT replaces the built-in filmic curve, matrix, saturation,
channel-gamma and split-color stock stage, while direct flash, exposure/white
balance, optics, grain and framing remain active. This avoids double-applying a
film curve while preventing a LUT from suppressing camera-specific lighting.

The disposable preset has a separate direct-flash proxy before tone mapping.
Because an RGB image has no scene depth, it cannot reproduce physically exact
inverse-square illumination or cast shadows. It uses underexposed ambient, a
broad center-weighted frontal beam and neutral screen-like fill to reproduce
the recognizable bright-near-field/dark-background separation without claiming
to infer geometry that is not present.

An already processed iPhone HEIC cannot be perfectly inverted to scene-linear
data. SDR HEIC/JPEG and ProRAW therefore require separate input paths.

## Backend plan

The production adapter will use libvips for demand-driven evaluation, bounded
memory, decoding, encoding and ordinary image operations. Custom effects can be
implemented as generated images or VipsOperation subclasses. HEIC/AVIF enters
through libheif; RAW enters through LibRaw. GIMP support should be a thin plug-in
around the same public library rather than a separate effect implementation.
