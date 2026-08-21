#ifndef POLAJUICE_H
#define POLAJUICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_VERSION_MAJOR 1
#define PJ_VERSION_MINOR 0
#define PJ_VERSION_PATCH 0

typedef struct PjImage PjImage;
typedef struct PjLut3D PjLut3D;

typedef struct {
    char message[256];
} PjError;

typedef struct {
    uint64_t seed;
    float strength;
    float age;   /* 0..1 storage-degradation axis: base fog, contrast decay,
                    desaturation, warm-magenta drift. Orthogonal to camera
                    and film; applies after the color stage. */
    float push;  /* development push/pull in stops, -1..+2: contrast and
                    grain rise with push, soften with pull. Chemistry, so
                    it applies after the color stage and survives LUTs.
                    Ignored for instant cameras (no user development). */
    bool cross_process;  /* E-6-in-C-41 style cross development: high
                    contrast, green-yellow highlights, crossed curves.
                    Also ignored for instant cameras. */
    const PjLut3D *color_lut;
} PjRenderOptions;

PjImage *pj_image_new(size_t width, size_t height, PjError *error);

/* Format chosen from the file extension: .ppm, .png, .jpg/.jpeg.
 * JPEG loading honors the EXIF orientation tag. */
PjImage *pj_image_load(const char *path, PjError *error);
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

PjImage *pj_render(const PjImage *input,
                   const char *preset_name,
                   const PjRenderOptions *options,
                   PjError *error);

const char *pj_version_string(void);

#ifdef __cplusplus
}
#endif

#endif
