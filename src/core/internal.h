#ifndef POLAJUICE_INTERNAL_H
#define POLAJUICE_INTERNAL_H

#define PJ_FILM_SLIDE    1u   /* reversal: E-6 and K-14 */
#define PJ_FILM_NEGATIVE 2u   /* C-41 color negative */
#define PJ_FILM_BW       4u   /* black-and-white (incl. C-41 XP2) */
#define PJ_FILM_INTEGRAL 8u   /* Polaroid integral packs */
#define PJ_FILM_PACK     16u  /* peel-apart pack film */

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
    const char *default_film;   /* canonical .cube stem in data/luts, or NULL */
    unsigned film_processes;    /* bitmask of PJ_FILM_* the camera accepts;
                                   0 = sealed process, takes no film at all */
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
    /* instant-frame geometry in millimeters; zero fields fall back to the
     * integral 600-series print (image 78.94x76.80, card 88.47x107.52,
     * top border equal to the sides, remainder as the bottom chin) */
    float frame_image_w_mm, frame_image_h_mm;
    float frame_outer_w_mm, frame_outer_h_mm;
    float grain_chroma;  /* 0 = luminance grain; 1 = independent per-channel
                            noise (dyed-starch Autochrome pointillism) */
} PjPreset;

void pj_set_error(PjError *error, const char *format, ...);
const PjPreset *pj_find_preset(const char *name);
const PjPreset *pj_preset_at(size_t index);

#endif
