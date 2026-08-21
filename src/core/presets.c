#include "internal.h"

#include <stdio.h>

#include <string.h>

#define IDENTITY {1,0,0, 0,1,0, 0,0,1}

static const PjPreset presets[] = {
    {
        .name = "35mm-negative",
        .film_processes = PJ_FILM_NEGATIVE,
        .default_film = "kodak_portra_400",
        .description = "Clean 35mm color negative: fine grain, gentle shoulder",
        .exposure_ev = 0.00f, .contrast = 1.05f, .black_lift = 0.006f,
        .highlight_rolloff = 0.70f, .saturation = 1.08f,
        .temperature = 0.035f, .tint = 0.00f, .vignette = 0.06f,
        .softness = 0.08f, .halation = 0.045f, .halation_radius = 4,
        .grain = 0.014f, .grain_scale = 1.0f, .grain_midtone_bias = 0.65f,
        .fade = 0.00f,
        .matrix = {1.025f,-0.015f,-0.010f, -0.008f,1.018f,-0.010f, 0.000f,-0.018f,1.018f},
        .channel_gamma = {0.99f,1.00f,1.02f},
        .shadow_tint = {0.003f,0.001f,-0.003f},
        .highlight_tint = {0.007f,0.003f,-0.004f}
    },
    {
        .name = "polaroid-600",
        .film_processes = PJ_FILM_INTEGRAL,
        .default_film = "polaroid_px-680",
        .description = "Soft, nearly grainless Color 600-style instant print",
        .exposure_ev = 0.05f, .contrast = 1.18f, .black_lift = 0.018f,
        .highlight_rolloff = 0.34f, .saturation = 1.04f,
        .temperature = 0.025f, .tint = 0.00f, .vignette = 0.06f,
        .softness = 0.68f, .halation = 0.030f, .halation_radius = 6,
        .grain = 0.004f, .grain_scale = 1.0f, .grain_midtone_bias = 0.90f,
        .fade = 0.00f,
        .matrix = {1.045f,-0.025f,-0.020f, -0.015f,1.035f,-0.020f, -0.010f,-0.030f,1.040f},
        .channel_gamma = {0.97f,1.00f,1.04f},
        .shadow_tint = {-0.004f,0.000f,0.006f},
        .highlight_tint = {0.014f,0.007f,-0.022f},
        .instant_frame = true
    },
    {
        .name = "super8",
        .weave = 0.0040f, .weave_period = 4.0f, .flicker_ev = 0.055f,
        .film_processes = PJ_FILM_SLIDE,
        .default_film = "kodak_ektachrome_100_vs",
        .description = "Super 8 reversal frame: 1.36:1 gate, coarse grain, halation",
        .crop_aspect = 1.362f,
        .exposure_ev = -0.02f, .contrast = 1.18f, .black_lift = 0.003f,
        .highlight_rolloff = 0.42f, .saturation = 1.10f,
        .temperature = 0.015f, .tint = 0.00f, .vignette = 0.08f,
        .softness = 0.22f, .halation = 0.090f, .halation_radius = 6,
        .grain = 0.024f, .grain_scale = 1.5f, .grain_midtone_bias = 0.72f,
        .fade = 0.00f,
        .matrix = {1.040f,-0.020f,-0.020f, -0.015f,1.030f,-0.015f, -0.010f,-0.025f,1.035f},
        .channel_gamma = {0.99f,1.00f,1.01f},
        .shadow_tint = {0.000f,0.000f,0.002f},
        .highlight_tint = {0.004f,0.002f,-0.003f}
    },
    {
        .name = "disposable-flash",
        .film_processes = PJ_FILM_NEGATIVE | PJ_FILM_BW,
        .default_film = "fuji_superia_800",
        .description = "ISO 800 disposable: harsh near flash, dark falloff and soft lens",
        .exposure_ev = 0.02f, .contrast = 1.20f, .black_lift = 0.002f,
        .highlight_rolloff = 0.32f, .saturation = 1.06f,
        .temperature = -0.015f, .tint = 0.00f,
        .flash_ev = 1.00f, .flash_ambient_ev = -0.45f,
        .flash_fill = 0.05f, .flash_falloff = 3.20f, .flash_cool = 0.05f,
        .vignette = 0.18f,
        .softness = 0.46f, .halation = 0.060f, .halation_radius = 4,
        .grain = 0.030f, .grain_scale = 1.35f, .grain_midtone_bias = 0.62f,
        .fade = 0.00f,
        .matrix = {1.015f,-0.010f,-0.005f, -0.010f,1.025f,-0.015f, -0.005f,0.010f,0.995f},
        .channel_gamma = {1.00f,1.00f,1.01f},
        .shadow_tint = {-0.002f,0.001f,0.004f},
        .highlight_tint = {0.002f,0.002f,0.000f}
    },
    {
        .name = "35mm-slide",
        .film_processes = PJ_FILM_SLIDE,
        .default_film = "fuji_provia_100f",
        .description = "35mm reversal slide: very fine grain, hard shoulder, rich color",
        .exposure_ev = -0.03f, .contrast = 1.15f, .black_lift = 0.001f,
        .highlight_rolloff = 0.40f, .saturation = 1.08f,
        .temperature = 0.00f, .tint = 0.00f, .vignette = 0.04f,
        .softness = 0.03f, .halation = 0.030f, .halation_radius = 3,
        .grain = 0.009f, .grain_scale = 0.8f, .grain_midtone_bias = 0.78f,
        .fade = 0.00f,
        .matrix = {1.025f,-0.012f,-0.013f, -0.012f,1.024f,-0.012f, -0.008f,-0.014f,1.022f},
        .channel_gamma = {1.00f,1.00f,1.00f},
        .shadow_tint = {0.000f,0.000f,0.002f},
        .highlight_tint = {0.001f,0.001f,0.000f}
    },
    {
        .name = "bw-35",
        .film_processes = PJ_FILM_BW,
        .default_film = "ilford_hp_5_plus_400",
        .description = "35mm ISO 400 black-and-white: medium contrast, honest grain",
        .exposure_ev = 0.00f, .contrast = 1.06f, .black_lift = 0.006f,
        .highlight_rolloff = 0.58f, .saturation = 0.0f,
        .temperature = 0.0f, .tint = 0.0f, .vignette = 0.10f,
        .softness = 0.06f, .halation = 0.020f, .halation_radius = 3,
        .grain = 0.032f, .grain_scale = 1.25f, .grain_midtone_bias = 0.58f,
        .fade = 0.00f, .matrix = IDENTITY,
        .channel_gamma = {1.00f,1.00f,1.00f}, .monochrome = true
    },
    {
        .name = "toy-camera-120",
        .film_processes = PJ_FILM_SLIDE | PJ_FILM_NEGATIVE | PJ_FILM_BW,
        .default_film = "lomography_x-pro_slide_200",
        .description = "Square plastic-lens camera with vignette and edge softness",
        .exposure_ev = 0.02f, .contrast = 1.08f, .black_lift = 0.010f,
        .highlight_rolloff = 0.58f, .saturation = 1.12f,
        .temperature = 0.035f, .tint = 0.015f, .vignette = 0.45f,
        .softness = 1.05f, .halation = 0.050f, .halation_radius = 6,
        .grain = 0.018f, .grain_scale = 1.15f, .grain_midtone_bias = 0.62f,
        .fade = 0.01f,
        .matrix = {1.035f,-0.020f,-0.015f, -0.012f,1.025f,-0.013f, -0.006f,-0.020f,1.026f},
        .channel_gamma = {0.99f,1.00f,1.02f},
        .shadow_tint = {0.002f,0.000f,-0.002f},
        .highlight_tint = {0.005f,0.002f,-0.004f},
        .square_crop = true
    },
    {
        .name = "cinestill-night",
        .film_processes = PJ_FILM_NEGATIVE,
        .description = "Remjet-stripped cine stock: strong red halation, night neon",
        .exposure_ev = 0.00f, .contrast = 1.12f, .black_lift = 0.004f,
        .highlight_rolloff = 0.45f, .saturation = 1.05f,
        .temperature = -0.020f, .tint = 0.00f, .vignette = 0.06f,
        .softness = 0.08f, .halation = 0.180f, .halation_radius = 9,
        .grain = 0.028f, .grain_scale = 1.4f, .grain_midtone_bias = 0.60f,
        .fade = 0.00f,
        .matrix = {1.030f,-0.015f,-0.015f, -0.012f,1.024f,-0.012f, -0.008f,-0.016f,1.024f},
        .channel_gamma = {1.00f,1.00f,1.01f},
        .shadow_tint = {-0.004f,0.001f,0.006f},
        .highlight_tint = {0.008f,0.004f,-0.006f}
    },
    {
        .name = "polaroid-sx70",
        .film_processes = PJ_FILM_INTEGRAL,
        .description = "SX-70-era integral print: dreamier and warmer than 600",
        .default_film = "polaroid_px-70",
        .exposure_ev = 0.02f, .contrast = 1.10f, .black_lift = 0.020f,
        .highlight_rolloff = 0.36f, .saturation = 0.92f,
        .temperature = 0.030f, .tint = 0.00f, .vignette = 0.10f,
        .softness = 0.85f, .halation = 0.030f, .halation_radius = 6,
        .grain = 0.004f, .grain_scale = 1.0f, .grain_midtone_bias = 0.90f,
        .fade = 0.04f,
        .matrix = {1.020f,-0.008f,-0.012f, -0.014f,1.020f,-0.006f, -0.010f,-0.014f,1.024f},
        .channel_gamma = {0.99f,1.00f,1.02f},
        .shadow_tint = {0.004f,0.002f,-0.006f},
        .highlight_tint = {0.016f,0.008f,-0.020f},
        .instant_frame = true
    },
    {
        .name = "polaroid-packfilm",
        .film_processes = PJ_FILM_PACK,
        .description = "Peel-apart pack film in a Land camera: crisp, thin even border",
        .default_film = "polaroid_669",
        .exposure_ev = 0.00f, .contrast = 1.14f, .black_lift = 0.010f,
        .highlight_rolloff = 0.40f, .saturation = 1.00f,
        .temperature = 0.010f, .tint = 0.00f, .vignette = 0.08f,
        .softness = 0.18f, .halation = 0.025f, .halation_radius = 5,
        .grain = 0.005f, .grain_scale = 1.0f, .grain_midtone_bias = 0.85f,
        .fade = 0.02f,
        .matrix = {1.015f,-0.006f,-0.009f, -0.010f,1.014f,-0.004f, -0.006f,-0.010f,1.016f},
        .channel_gamma = {1.00f,1.00f,1.01f},
        .shadow_tint = {0.002f,0.001f,-0.003f},
        .highlight_tint = {0.008f,0.005f,-0.008f},
        .instant_frame = true,
        .frame_image_w_mm = 73.0f, .frame_image_h_mm = 95.0f,
        .frame_outer_w_mm = 82.6f, .frame_outer_h_mm = 108.0f
    },
    {
        .name = "midcentury-rangefinder",
        .film_processes = PJ_FILM_SLIDE | PJ_FILM_NEGATIVE | PJ_FILM_BW,
        .description = "Late-1940s folding 35mm: uncoated-lens flare, early Kodachrome era",
        .default_film = "kodak_kodachrome_64",
        .exposure_ev = -0.02f, .contrast = 0.96f, .black_lift = 0.030f,
        .highlight_rolloff = 0.48f, .saturation = 0.98f,
        .temperature = 0.012f, .tint = 0.00f, .vignette = 0.14f,
        .softness = 0.10f, .halation = 0.020f, .halation_radius = 4,
        .grain = 0.006f, .grain_scale = 0.9f, .grain_midtone_bias = 0.80f,
        .fade = 0.00f,
        .matrix = {1.010f,-0.004f,-0.006f, -0.006f,1.008f,-0.002f, -0.004f,-0.008f,1.012f},
        .channel_gamma = {0.99f,1.00f,1.00f},
        .shadow_tint = {0.003f,0.002f,0.001f},
        .highlight_tint = {0.008f,0.005f,-0.004f}
    },
    {
        .name = "technicolor-3strip",
        .weave = 0.0015f, .weave_period = 6.0f, .flicker_ev = 0.020f,
        .film_processes = 0,
        .description = "Three-strip dye-transfer cinema: saturated, smooth, Academy gate",
        .exposure_ev = 0.03f, .contrast = 1.15f, .black_lift = 0.010f,
        .highlight_rolloff = 0.50f, .saturation = 1.30f,
        .temperature = 0.005f, .tint = 0.00f, .vignette = 0.05f,
        .softness = 0.10f, .halation = 0.055f, .halation_radius = 5,
        .grain = 0.004f, .grain_scale = 1.0f, .grain_midtone_bias = 0.85f,
        .fade = 0.00f,
        .matrix = {1.110f,-0.055f,-0.055f, -0.050f,1.100f,-0.050f, -0.040f,-0.060f,1.100f},
        .channel_gamma = {0.99f,1.00f,1.00f},
        .shadow_tint = {-0.002f,0.000f,0.004f},
        .highlight_tint = {0.010f,0.006f,-0.006f},
        .crop_aspect = 1.375f
    },
    {
        .name = "autochrome",
        .weave = 0.0060f, .weave_period = 3.0f, .flicker_ev = 0.100f,
        .film_processes = 0,
        .description = "Lumiere Autochrome plate: pastel pointillist colored grain",
        .exposure_ev = 0.00f, .contrast = 0.92f, .black_lift = 0.030f,
        .highlight_rolloff = 0.55f, .saturation = 0.72f,
        .temperature = 0.015f, .tint = 0.00f, .vignette = 0.15f,
        .softness = 0.45f, .halation = 0.080f, .halation_radius = 6,
        .grain = 0.050f, .grain_scale = 2.2f, .grain_midtone_bias = 0.45f,
        .grain_chroma = 1.0f,
        .fade = 0.06f,
        .matrix = {1.010f,-0.004f,-0.006f, -0.006f,1.008f,-0.002f, -0.004f,-0.008f,1.010f},
        .channel_gamma = {0.99f,1.00f,1.01f},
        .shadow_tint = {0.004f,0.003f,-0.002f},
        .highlight_tint = {0.014f,0.010f,-0.006f},
        .crop_aspect = 1.333f
    }
};


char *pj_preset_traits(const char *name, char *buffer, size_t size)
{
    const PjPreset *p = pj_find_preset(name);
    if (!p || !buffer || size == 0) return NULL;
    buffer[0] = '\0';
    size_t used = 0;
#define ADD(text) do { \
        int n = snprintf(buffer + used, size - used, "%s%s", \
                         used ? ", " : "", (text)); \
        if (n > 0 && (size_t)n < size - used) used += (size_t)n; \
    } while (0)
    if (p->instant_frame) ADD("instant-print crop and frame");
    if (p->square_crop) ADD("square crop");
    if (p->crop_aspect > 0.0f) {
        char gate[32];
        snprintf(gate, sizeof gate, "%.2f:1 gate crop",
                 (double)p->crop_aspect);
        ADD(gate);
    }
    if (p->flash_ev > 0.0f) ADD("direct on-camera flash");
    if (p->softness >= 0.5f) ADD("very soft lens");
    else if (p->softness >= 0.12f) ADD("soft lens");
    if (p->vignette >= 0.20f) ADD("heavy vignette");
    else if (p->vignette >= 0.08f) ADD("mild vignette");
    if (p->halation >= 0.10f) ADD("strong red halation");
    else if (p->halation >= 0.03f) ADD("halation on highlights");
    if (p->grain_chroma > 0.5f) ADD("pointillist colored grain");
    else if (p->grain >= 0.025f) ADD("coarse grain");
    else if (p->grain >= 0.012f) ADD("visible grain");
    else ADD("fine grain");
    if (p->monochrome) ADD("black and white");
    if (p->fade > 0.05f) ADD("slightly aged by default");
#undef ADD
    return buffer;
}


static unsigned process_bit(const char *process)
{
    if (!process) return 0;
    if (!strcmp(process, "slide")) return PJ_FILM_SLIDE;
    if (!strcmp(process, "negative")) return PJ_FILM_NEGATIVE;
    if (!strcmp(process, "bw")) return PJ_FILM_BW;
    if (!strcmp(process, "integral")) return PJ_FILM_INTEGRAL;
    if (!strcmp(process, "pack")) return PJ_FILM_PACK;
    return 0;
}

bool pj_preset_accepts_film(const char *camera, const char *process)
{
    const PjPreset *preset = pj_find_preset(camera);
    unsigned bit = process_bit(process);
    return preset && bit && (preset->film_processes & bit);
}

char *pj_preset_film_processes(const char *camera, char *buffer, size_t size)
{
    const PjPreset *preset = pj_find_preset(camera);
    if (!preset || !buffer || size == 0) return NULL;
    buffer[0] = '\0';
    size_t used = 0;
#define ADDP(bit, text) do { \
        if (preset->film_processes & (bit)) { \
            int n = snprintf(buffer + used, size - used, "%s%s", \
                             used ? ", " : "", (text)); \
            if (n > 0 && (size_t)n < size - used) used += (size_t)n; \
        } \
    } while (0)
    ADDP(PJ_FILM_SLIDE, "slide (E-6/K-14)");
    ADDP(PJ_FILM_NEGATIVE, "color negative (C-41)");
    ADDP(PJ_FILM_BW, "black-and-white");
    ADDP(PJ_FILM_INTEGRAL, "Polaroid integral");
    ADDP(PJ_FILM_PACK, "peel-apart pack film");
#undef ADDP
    return buffer;
}

bool pj_preset_is_instant(const char *name)
{
    const PjPreset *preset = pj_find_preset(name);
    return preset && preset->instant_frame;
}

const char *pj_preset_default_film(const char *name)
{
    const PjPreset *preset = pj_find_preset(name);
    return preset ? preset->default_film : NULL;
}

size_t pj_preset_count(void) { return sizeof presets / sizeof presets[0]; }

const PjPreset *pj_preset_at(size_t index)
{
    return index < pj_preset_count() ? &presets[index] : NULL;
}

const PjPreset *pj_find_preset(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < pj_preset_count(); ++i)
        if (strcmp(name, presets[i].name) == 0) return &presets[i];
    return NULL;
}

const char *pj_preset_name(size_t index)
{
    const PjPreset *preset = pj_preset_at(index);
    return preset ? preset->name : NULL;
}

const char *pj_preset_description(const char *name)
{
    const PjPreset *preset = pj_find_preset(name);
    return preset ? preset->description : NULL;
}
