# Preset data

Version 0.1 compiles its seven recipes into `src/core/presets.c` so the core is
dependency-free. Version 0.2 will load schema-versioned JSON recipes from this
directory. Binary LUTs should live in a sibling `luts/` directory and be
identified by a content hash in the recipe.

## Fetching stock LUTs

As of 0.4.0 each camera applies its canonical film automatically when the
film library is installed; the table below documents those defaults and
sensible alternatives. `-f NAME` overrides, `--no-film` forces scalars.

`make fetch-luts` (or `scripts/fetch_luts.sh`) downloads the G'MIC color-preset
family packs, already in `.cube` format, into `data/luts/`:

| built-in preset   | family pack       | suggested starting cube          |
|-------------------|-------------------|----------------------------------|
| polaroid-600      | instant_consumer  | polaroid_px-680.cube             |
| 35mm-slide        | colorslide        | an Ektachrome/Provia variant     |
| super8            | colorslide        | an Ektachrome variant            |
| bw-35             | bw                | an Ilford HP5 variant            |
| disposable-flash  | negative_old      | a Fuji Superia 800 variant       |
| 35mm-negative     | negative_new      | a Portra/Fuji C variant          |
| toy-camera-120    | (creative choice) | try cross-processed slide cubes  |

Pair one LUT with its matching preset; the preset supplies the optics
(softness, vignette, halation, grain, framing) and the LUT the stock color.
Passing one LUT to every preset makes the outputs nearly identical by design,
since the LUT replaces the whole built-in tone/color stage.

`scripts/fetch_luts.sh --rawtherapee` additionally fetches the RawTherapee
Film Simulation Collection (~402 MB of HaldCLUT PNGs) and converts each PNG
to `.cube` with `tools/haldclut2cube.py` (needs python3 + Pillow).

New in 0.2.0:

| built-in preset   | family pack       | suggested starting cube          |
|-------------------|-------------------|----------------------------------|
| cinestill-night   | (PictureFX)       | a CineStill 800T cube            |

Aging is the `--age` option (0..1), not a camera: it composes with every
camera/film pairing above.
