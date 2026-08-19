# Roadmap

## 0.1.2 — distinct lighting and stock stages

- retain preset lighting and input balance when an external stock LUT is supplied
- add a direct-flash proxy with underexposed ambient and near-field falloff
- regression tests for LUT/lighting composition and visible flash separation

## 0.1.1 — sourced correction

- standard `.cube` 3D LUT loading and trilinear interpolation
- luminance-only grain with midtone bias
- manufacturer/reputable-source rationale for every preset family
- restrained grain and halation across all presets
- corrected 600/i-Type frame geometry

## 0.2 — real-world input

- libvips-backed JPEG, PNG and TIFF I/O
- embedded ICC profile handling through Little CMS
- EXIF orientation and metadata policy
- external JSON recipe files with schema versioning and inheritance
- optional tetrahedral interpolation for existing `.cube` support

## 0.3 — iPhone input

- HEIC/Display P3 decoding
- explicit SDR and HDR paths
- conservative computational-photography normalization
- optional ProRAW input through LibRaw

## 0.4 — calibrated stocks

- paired chart-capture calibration utility
- fitted characteristic curves
- exposure-dependent grain statistics
- separate stock and scanner/print models
- perceptual visual-regression suite

## 0.5 — integrations

- GIMP 3 plug-in
- contact-sheet and comparison commands
- shell completion and man page
- Homebrew and Debian packaging

## 0.6 — motion

- FFmpeg adapter
- temporally coherent grain
- gate weave, exposure flicker and persistent scratches
- Super 8 and 16 mm recipes

- Orange quartz date stamp overlay (small bitmap font, output stage);
  identity-defining for disposable-flash and 35mm-negative.
