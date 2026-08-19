#ifndef POLAJUICE_H
#define POLAJUICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_VERSION_MAJOR 0
#define PJ_VERSION_MINOR 2
#define PJ_VERSION_PATCH 0

typedef struct PjImage PjImage;
typedef struct PjLut3D PjLut3D;

typedef struct {
    char message[256];
} PjError;

typedef struct {
    uint64_t seed;
    float strength;
    const PjLut3D *color_lut;
} PjRenderOptions;

PjImage *pj_image_new(size_t width, size_t height, PjError *error);
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

PjImage *pj_render(const PjImage *input,
                   const char *preset_name,
                   const PjRenderOptions *options,
                   PjError *error);

const char *pj_version_string(void);

#ifdef __cplusplus
}
#endif

#endif
