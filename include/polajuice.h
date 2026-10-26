#ifndef POLAJUICE_H
#define POLAJUICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_VERSION_MAJOR 1
#define PJ_VERSION_MINOR 20
#define PJ_VERSION_PATCH 0

typedef struct PjImage PjImage;
typedef struct PjLut3D PjLut3D;

typedef struct {
    char message[256];
} PjError;

/* Sentinel for PjRenderOptions.halation meaning "no halation at all";
 * 0 keeps the preset's default so that zero-initialized options render
 * exactly as before. */
#define PJ_HALATION_OFF (-1.0f)

typedef struct {
    uint64_t seed;
    float strength;
    float age;   /* 0..1 storage-degradation axis: base fog, contrast decay,
                    desaturation, warm-magenta drift. Orthogonal to camera
                    and film; applies after the color stage. */
    float push;  /* development push/pull in stops, PJ_PUSH_MIN..PJ_PUSH_MAX
                    (-3..+4): contrast and grain rise with push, soften
                    with pull. Within -1..+2 the response is linear, as
                    through 1.20; each further stop counts 0.6 (contrast
                    gains saturate, grain keeps coarsening less than
                    linearly). Chemistry, so it applies after the color
                    stage and survives LUTs. Ignored for instant cameras
                    (no user development). Fractional stops are fine. */
    bool cross_process;  /* E-6-in-C-41 style cross development: high
                    contrast, green-yellow highlights, crossed curves.
                    Also ignored for instant cameras. */
    bool temporal;  /* movie mode: enables per-frame gate weave, exposure
                    flicker, and frame-decorrelated grain for cameras that
                    define them. Stills leave this false and render
                    exactly as before. */
    int64_t frame;  /* frame index when temporal; also usable from a
                    stills front-end to render "frame N of a reel". */
    float motion_scale;  /* scales gate weave and flicker amplitudes;
                    0 uses the default of 1.0. Grain is unaffected.
                    The calibration knob for matching real transports. */
    const PjLut3D *color_lut;
    const PjLut3D *print_lut;   /* optional print/scan stock transform,
                    chained after the film transform - the cinema
                    negative->print model (e.g. Vision3 through 2383). */
    const char *film_process;   /* film process token ("slide", "negative",
                    "bw", "integral", "pack") or NULL when unknown. Age
                    uses it: dye layers fade differently per process. */
    const char *contrast_filter; /* lens contrast filter name applied to the
                    light BEFORE the film: "yellow" (#8), "orange" (#15),
                    "red" (#25), "green" (#58), "blue" (#47), or NULL/
                    "none". Exposure is renormalized (the filter factor a
                    metering camera would apply), so the drama is in the
                    tonal relationships: orange+bw darkens skies, red is
                    violent about it, green lightens foliage. On color
                    stock it is the strong period cast it always was. */
    const char *film_stem;      /* film stock stem (e.g. "ilford_delta_3200")
                    or NULL. Grain derives speed and granularity from it:
                    amount scales ~sqrt(ISO) against the camera's canonical
                    stock, texture by grain family (tabular finer, dye
                    clouds softer, HIE grittier), push suffixes (_+, _++)
                    add stops. NULL leaves the camera's grain untouched. */
    const char *grain_model;    /* "classic" (NULL): additive display noise
                    at the end of the pipeline, as through 1.15.
                    "silver": grain generated from exposure inside the
                    film - binomial grain statistics set the amplitude,
                    and texture follows exposure (low exposure develops
                    only the large fast grains, so shadows run coarse,
                    highlights fine). It sits before the print stock, push
                    and age, which act on it as they act on the image. */
    float ev;       /* exposure adjustment in stops applied to the scene
                    light before anything else. 0 leaves display-referred
                    input untouched. Scene-linear input (raw, --linear)
                    always opens one stop up on top of this, with the
                    sensor's top stop rolled in by a shoulder before the
                    film sees it. */
    float halation; /* scales the camera's halation: 0 means unset (the
                    preset's own amount, so zeroed options render as
                    before), 1 the same, 2 twice the glow, PJ_HALATION_OFF
                    none. The glow is the brightest light scattering back
                    through the base, so it is strongest with raw input,
                    where speculars still exist above white. */
    float edge;     /* development edge effects, 0 (off) .. 2: 1 is a dilute
                    developer, 2 stand development. Developer exhausts
                    locally, so the dense side of a boundary develops
                    denser and a small dense detail more than a large one
                    (border, fringe and Eberhard effects). Nonlinear and
                    asymmetric, unlike an unsharp mask; uniform areas are
                    untouched. Ignored for instant cameras. */
    /* Scene adjustments, applied to the light before the camera stage:
     * all scene-linear, all identity at 0, so zeroed options render as
     * before. They describe the scene and the framing; the film is left
     * to do what it does. */
    float temp;     /* white balance, warm/cool: stops of red-to-blue ratio,
                    +1 a stop warmer (R x sqrt2, B / sqrt2); -2..+2 */
    float tint;     /* green/magenta: +1 a stop toward magenta (G halved
                    against R and B); -1..+1 */
    float black;    /* black point, in scene units of display white: positive
                    crushes (0.02 sends the bottom 2% to black and rescales),
                    negative fogs (lifts everything by that much); -0.1..0.1 */
    float shadows;  /* lift (+) or drop (-) the shadows, in stops at four
                    stops below middle gray, fading to nothing at gray;
                    hue and saturation kept; -2..+2 */
    float highlights; /* moves the raw shoulder's knee: 0 keeps it at 0.75 of
                    display white, -1 brings it down to 0.5 (more headroom
                    rolled in, softer top), +1 raises it to 0.9 (harder);
                    scene-linear input only, since display-referred input
                    has no headroom; -1..+1 */
    float contrast; /* scene contrast: a log gamma about middle gray, +1 is
                    gamma 1.41, -1 is 0.71; applied to luminance, color
                    ratios kept; -1..+1 */
    float grad;     /* graduated neutral-density filter: density in stops at
                    the dark edge, 0..3; the filter you would have put on
                    the lens for that sky */
    float grad_angle; /* where the dark edge is: 0 top, 90 right, 180
                    bottom, 270 left; degrees */
    float grad_pos; /* the transition's center, as an offset from the frame
                    center along the gradient, -0.5..0.5 of the frame; the
                    transition feathers over a quarter of the frame */
    /* Lens and framing, before the scene adjustments: the input is
     * corrected, straightened and trimmed, and everything after sees that
     * frame. All identity at 0. */
    float denoise;  /* chroma denoise, 0..3: the color planes blurred over
                    about 2 px per unit at full size, luminance untouched;
                    for high-ISO raw before the grain models add theirs */
    float distortion; /* radial lens correction, -0.3..0.3: positive undoes
                    barrel (corners pulled in by the lens), negative
                    pincushion; r' = r(1 - k r^2), r in half-diagonals */
    float devignette; /* undo the lens's corner falloff, 0..2 stops at the
                    corners, rising with r^2 */
    float rotate;   /* straighten, degrees, -15..15, positive clockwise;
                    the canvas stays, corners outside come in black - trim
                    them */
    float trim_x, trim_y, trim_w, trim_h; /* a crop of the frame in
                    fractions 0..1 (x, y, width, height), before the
                    camera's format crop; width or height 0 = no trim */
    int crop;       /* PJ_CROP_CAMERA (0): the camera's own format - the
                    square of the 126 cartridge, the 1.03:1 instant print,
                    the Super 8 gate - cut from the middle of the input,
                    as always. PJ_CROP_FULL: keep the whole input frame at
                    its own aspect; an instant camera then puts its
                    border around the full frame. Cameras without a
                    format crop render the same either way. */
} PjRenderOptions;

#define PJ_CROP_CAMERA 0
#define PJ_CROP_FULL 1
/* "camera" or "full" -> PJ_CROP_*; -1 for anything else. */
int pj_crop_parse(const char *name);
const char *pj_crop_name(int crop);

/* Where the picture lands in a render: the crop taken from an in_w x
 * in_h input (full input when the camera has no format crop or crop is
 * PJ_CROP_FULL) and where that crop sits in the out_w x out_h output -
 * at the origin, except inside an instant print's border. Crop and
 * output are at the input's pixel scale. */
typedef struct {
    size_t in_w, in_h;          /* the prepared frame (after rotate and trim) */
    size_t crop_x, crop_y, crop_w, crop_h;
    size_t place_x, place_y;
    size_t out_w, out_h;
    size_t orig_w, orig_h;      /* the file's frame, before preparation */
    size_t trim_x, trim_y;      /* where the prepared frame sits in it */
    float rotate;               /* degrees clockwise applied about its center */
} PjGeometry;
bool pj_render_geometry(const char *preset, size_t in_w, size_t in_h, int crop,
                        PjGeometry *g);
/* The same with the lens and framing options applied first (rotate and
 * trim change the frame the camera sees). */
bool pj_render_geometry_opt(const char *preset, size_t in_w, size_t in_h,
                            const PjRenderOptions *options, PjGeometry *g);
/* One comment line for a JPEG's recipe comment:
 * "# geometry in=W,H crop=X,Y,W,H place=X,Y out=W,H\n" */
size_t pj_geometry_format(const PjGeometry *g, char *buf, size_t size);

/* The scene adjustments as named, ranged floats, for every front end
 * that reads them as text: pj_scene_keys() lists them, pj_scene_set()
 * parses "key=value" style pairs into the options (false when the key
 * is not a scene key or the value is out of range). */
typedef struct { const char *key; float lo, hi; } PjSceneKey;
const PjSceneKey *pj_scene_keys(size_t *count);
bool pj_scene_is_key(const char *key);
bool pj_scene_set(PjRenderOptions *o, const char *key, const char *value);
float pj_scene_get(const PjRenderOptions *o, const char *key);

#define PJ_PUSH_MIN (-3.0f)
#define PJ_PUSH_MAX (4.0f)
/* Development mode text <-> options: "normal", "cross", "push+N",
 * "pull-N", "cross+N", "cross-N" (N in stops, fractional allowed, within
 * PJ_PUSH_MIN..PJ_PUSH_MAX). pj_develop_parse returns false and leaves
 * the outputs alone for anything else; pj_develop_format writes the
 * shortest text that reads back to the same values. */
bool pj_develop_parse(const char *text, float *push, bool *cross);
void pj_develop_format(float push, bool cross, char *buf, size_t size);

PjImage *pj_image_new(size_t width, size_t height, PjError *error);

/* Format chosen from the file extension: .ppm, .png, .jpg/.jpeg are
 * display-referred 8-bit sRGB decoded to linear; .nef and .dng are raw
 * camera files decoded to scene-linear RGB (sensor white at 1.0, headroom
 * above it kept). JPEG and raw loading honor the orientation tag. */
PjImage *pj_image_load(const char *path, PjError *error);
/* A 16-bit (or 8-bit) PNG or uncompressed RGB TIFF taken as scene-linear
 * with no gamma decode: for dcraw -4 -T and similar linear exports. */
PjImage *pj_image_load_linear(const char *path, PjError *error);
/* Copy the per-image flags (scene-linear) from one image to another. */
void pj_image_copy_flags(PjImage *to, const PjImage *from);
/* True when the image came through a scene-linear door. */
bool pj_image_is_scene_linear(const PjImage *image);
/* One line describing the structure of the last raw file loaded
 * (container, layout, bit depth, compression, tiling, camera). */
const char *pj_raw_description(void);
/* The straight print: opens a scene-linear image the stop the pipeline
 * would (plus ev), rolls the headroom in with the scene shoulder, and
 * marks it display-referred. No camera, no film. Display-referred input
 * is untouched. */
void pj_image_scene_to_display(PjImage *image, float ev);
bool pj_image_save(const PjImage *image, const char *path, PjError *error);

/* Box-average downscale in linear light so the long edge is at most
 * max_dim pixels; returns the input untouched when already small enough.
 * The pipeline is resolution-aware, so a downscaled render is a faithful
 * miniature of the full-size one. */
PjImage *pj_image_downscale(PjImage *image, size_t max_dim, PjError *error);

PjImage *pj_image_load_ppm(const char *path, PjError *error);
bool pj_image_save_ppm(const PjImage *image, const char *path, PjError *error);
void pj_image_free(PjImage *image);

size_t pj_image_width(const PjImage *image);
size_t pj_image_height(const PjImage *image);
float *pj_image_pixels(PjImage *image);
const float *pj_image_pixels_const(const PjImage *image);

PjLut3D *pj_lut3d_load_cube(const char *path, PjError *error);
void pj_lut3d_free(PjLut3D *lut);
size_t pj_lut3d_size(const PjLut3D *lut);
void pj_lut3d_apply(const PjLut3D *lut, float rgb[3]);

size_t pj_preset_count(void);
const char *pj_preset_name(size_t index);
const char *pj_preset_description(const char *name);
/* Canonical film-stock LUT stem for a camera preset, or NULL when the
 * scalar engine carries the look (e.g. expired-film). */
const char *pj_preset_default_film(const char *name);
/* Human-readable trait summary generated from the preset's own parameters
 * (framing, flash, lens, halation, grain, ...). Returns buffer, or NULL
 * for an unknown camera. */
char *pj_preset_traits(const char *name, char *buffer, size_t size);
/* True for integral/peel-apart instant cameras, whose development is
 * inside the film unit: push/pull and cross-processing do not apply. */
bool pj_preset_is_instant(const char *name);
/* The camera's format crop as a short label ("square", "1.03:1 instant
 * print", "1.36:1 gate"), or NULL when the camera keeps the input's
 * frame - the cameras PjRenderOptions.crop makes a difference to. */
const char *pj_preset_format(const char *name);
/* Film-process compatibility. Process tokens: "slide", "negative", "bw",
 * "integral", "pack". A sealed-process camera (autochrome,
 * technicolor-3strip) accepts none. pj_preset_film_processes writes a
 * human-readable list ("" for sealed) and returns buffer, or NULL for an
 * unknown camera. */
bool pj_preset_accepts_film(const char *camera, const char *process);
char *pj_preset_film_processes(const char *camera, char *buffer, size_t size);
/* The camera's sole process token when it accepts exactly one (e.g.
 * 35mm-slide -> "slide", polaroid-600 -> "integral"), else NULL. Used to
 * default the age profile when no film supplies a process. */
const char *pj_preset_primary_process(const char *camera);
bool pj_contrast_filter_known(const char *name);
/* True for NULL, "classic" and "silver". */
bool pj_grain_model_known(const char *name);

PjImage *pj_render(const PjImage *input,
                   const char *preset_name,
                   const PjRenderOptions *options,
                   PjError *error);

/* The recipe: the whole look as key=value text (docs/RECIPES.md).
 * film and print are library stems or "none" (the scalar engine / no
 * print). Writes at most size bytes, returns the length it needed. */
size_t pj_recipe_format(const char *camera, const char *film,
                        const char *print, const PjRenderOptions *options,
                        char *buf, size_t size);
bool pj_recipe_is_recipe(const char *text);

/* Text carried with the image: the recipe, embedded as a JPEG comment
 * on save so any render can be replayed with --recipe out.jpg. The
 * image keeps its own copy. NULL clears it. */
bool pj_image_set_comment(PjImage *image, const char *text);
const char *pj_image_comment(const PjImage *image);
/* The recipe embedded in a JPEG file, or NULL; the caller frees. */
char *pj_jpeg_read_comment(const char *path);

const char *pj_version_string(void);
/* Threads the pipeline will use (cores, or POLAJUICE_THREADS). */
unsigned pj_thread_count(void);

#ifdef __cplusplus
}
#endif

#endif
