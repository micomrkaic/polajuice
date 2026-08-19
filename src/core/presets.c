#include "internal.h"

#include <string.h>

#define IDENTITY {1,0,0, 0,1,0, 0,0,1}

static const PjPreset presets[] = {
    {
        .name = "35mm-negative",
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
        .name = "expired-film",
        .description = "Decades-old color negative: base fog, magenta drift, low contrast",
        .exposure_ev = 0.00f, .contrast = 0.96f, .black_lift = 0.040f,
        .highlight_rolloff = 0.62f, .saturation = 0.82f,
        .temperature = 0.020f, .tint = -0.010f, .vignette = 0.10f,
        .softness = 0.15f, .halation = 0.020f, .halation_radius = 4,
        .grain = 0.024f, .grain_scale = 1.3f, .grain_midtone_bias = 0.55f,
        .fade = 0.65f,
        .matrix = {1.010f,-0.005f,-0.005f, -0.008f,1.008f,-0.010f, -0.004f,-0.006f,1.010f},
        .channel_gamma = {0.98f,1.00f,0.99f},
        .shadow_tint = {0.006f,-0.002f,0.005f},
        .highlight_tint = {0.006f,0.004f,-0.002f}
    }
};

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
