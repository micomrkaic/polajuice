#ifndef POLAJUICE_INTERNAL_H
#define POLAJUICE_INTERNAL_H

#include "polajuice.h"

struct PjImage {
    size_t width;
    size_t height;
    float *rgb;
};

struct PjLut3D {
    size_t size;
    float domain_min[3];
    float domain_max[3];
    float *rgb;
};

typedef struct {
    const char *name;
    const char *description;
    float exposure_ev;
    float contrast;
    float black_lift;
    float highlight_rolloff;
    float saturation;
    float temperature;
    float tint;
    float flash_ev;
    float flash_ambient_ev;
    float flash_fill;
    float flash_falloff;
    float flash_cool;
    float vignette;
    float softness;
    float halation;
    unsigned halation_radius;
    float grain;
    float grain_scale;
    float grain_midtone_bias;
    float fade;
    float matrix[9];
    float channel_gamma[3];
    float shadow_tint[3];
    float highlight_tint[3];
    bool monochrome;
    bool square_crop;
    bool instant_frame;
    float crop_aspect;   /* > 0: center-crop to this width/height ratio */
} PjPreset;

void pj_set_error(PjError *error, const char *format, ...);
const PjPreset *pj_find_preset(const char *name);
const PjPreset *pj_preset_at(size_t index);

#endif
