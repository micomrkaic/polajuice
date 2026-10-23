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
    bool scene_linear;  /* raw / --linear input: values are scene-referred
                           with the sensor's white at 1.0 and headroom
                           above it preserved; the pipeline opens such
                           images a stop up and shoulders the headroom.
                           Display-referred (8-bit) input leaves this
                           false and renders exactly as before. */
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
    float weave;         /* gate weave amplitude as fraction of height */
    float weave_period;  /* frames per weave cycle (smoothness) */
    float flicker_ev;    /* exposure flicker amplitude in stops */
    /* instant-frame geometry in millimeters; zero fields fall back to the
     * integral 600-series print (image 78.94x76.80, card 88.47x107.52,
     * top border equal to the sides, remainder as the bottom chin) */
    float frame_image_w_mm, frame_image_h_mm;
    float frame_outer_w_mm, frame_outer_h_mm;
    float grain_chroma;  /* 0 = luminance grain; 1 = independent per-channel
                            noise (dyed-starch Autochrome pointillism) */
} PjPreset;

void pj_set_error(PjError *error, const char *format, ...);

/* parallel.c: run fn over [0, rows) in bands, one thread per core;
 * bit-identical to the serial loop at any thread count */
typedef void (*PjRowFn)(size_t y0, size_t y1, void *ctx);
void pj_parallel_rows(size_t rows, PjRowFn fn, void *ctx);
unsigned pj_thread_count(void);

/* image.c: the sRGB transfer through 8192-cell tables with linear
 * interpolation (error under 2e-5, a hundredth of an 8-bit level)
 * instead of powf per sample; pj_srgb_tables_init() is idempotent and
 * must run before any threaded stage (pj_render and the loaders call
 * it). pj_srgb_decode8 is the exact 8-bit decode for loaders. */
void pj_srgb_tables_init(void);
float pj_srgb_encode(float linear);
float pj_srgb_decode(float srgb);
float pj_srgb_decode8(unsigned char v);
/* rawdec.c: scene-linear doors (NEF, DNG; 16-bit TIFF/PNG as linear) */
PjImage *pj_image_load_raw(const char *path, PjError *error);
/* image_io.c: bake an EXIF/TIFF orientation (1..8) into the pixels; takes
 * ownership of image */
PjImage *pj_image_apply_orientation(PjImage *image, int orientation, PjError *error);
const PjPreset *pj_find_preset(const char *name);
const PjPreset *pj_preset_at(size_t index);

#endif
