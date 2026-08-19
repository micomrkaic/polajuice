#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static float mixf(float a, float b, float t) { return a + (b - a) * t; }

static float srgb_to_linear(float x)
{
    x = clamp01(x);
    return x <= 0.04045f ? x / 12.92f
                         : powf((x + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float x)
{
    x = clamp01(x);
    return x <= 0.0031308f ? 12.92f * x
                           : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

static PjImage *clone_image(const PjImage *input, PjError *error)
{
    PjImage *copy = pj_image_new(input->width, input->height, error);
    if (copy)
        memcpy(copy->rgb, input->rgb,
               input->width * input->height * 3 * sizeof(float));
    return copy;
}

static void box_blur(float *rgb, size_t width, size_t height, unsigned radius)
{
    if (radius == 0 || width == 0 || height == 0) return;
    float *temp = malloc(width * height * 3 * sizeof *temp);
    if (!temp) return;

    size_t initial_x = radius < width ? radius : width - 1;
    for (size_t y = 0; y < height; ++y) {
        for (size_t c = 0; c < 3; ++c) {
            double sum = 0.0;
            for (size_t x = 0; x <= initial_x; ++x)
                sum += rgb[(y * width + x) * 3 + c];
            size_t count = initial_x + 1;
            for (size_t x = 0; x < width; ++x) {
                temp[(y * width + x) * 3 + c] = (float)(sum / (double)count);
                if (x >= radius) {
                    sum -= rgb[(y * width + (x - radius)) * 3 + c];
                    --count;
                }
                if (x + radius + 1 < width) {
                    sum += rgb[(y * width + x + radius + 1) * 3 + c];
                    ++count;
                }
            }
        }
    }
    size_t initial_y = radius < height ? radius : height - 1;
    for (size_t x = 0; x < width; ++x) {
        for (size_t c = 0; c < 3; ++c) {
            double sum = 0.0;
            for (size_t y = 0; y <= initial_y; ++y)
                sum += temp[(y * width + x) * 3 + c];
            size_t count = initial_y + 1;
            for (size_t y = 0; y < height; ++y) {
                rgb[(y * width + x) * 3 + c] = (float)(sum / (double)count);
                if (y >= radius) {
                    sum -= temp[((y - radius) * width + x) * 3 + c];
                    --count;
                }
                if (y + radius + 1 < height) {
                    sum += temp[((y + radius + 1) * width + x) * 3 + c];
                    ++count;
                }
            }
        }
    }
    free(temp);
}

static void apply_softness(PjImage *image, float amount)
{
    if (amount <= 0.01f) return;
    float *blurred = malloc(image->width * image->height * 3 * sizeof *blurred);
    if (!blurred) return;
    size_t samples = image->width * image->height * 3;
    memcpy(blurred, image->rgb, samples * sizeof *blurred);
    float diagonal = hypotf((float)image->width, (float)image->height);
    unsigned radius = (unsigned)lrintf(amount * diagonal / 2200.0f);
    if (radius < 1) radius = 1;
    box_blur(blurred, image->width, image->height, radius);
    float blend = clamp01(amount / 2.0f);
    for (size_t i = 0; i < samples; ++i)
        image->rgb[i] = mixf(image->rgb[i], blurred[i], blend);
    free(blurred);
}

static void apply_vignette(PjImage *image, float amount)
{
    if (amount <= 0.0f) return;
    float cx = ((float)image->width - 1.0f) * 0.5f;
    float cy = ((float)image->height - 1.0f) * 0.5f;
    float invx = cx > 0.0f ? 1.0f / cx : 0.0f;
    float invy = cy > 0.0f ? 1.0f / cy : 0.0f;
    for (size_t y = 0; y < image->height; ++y) {
        for (size_t x = 0; x < image->width; ++x) {
            float dx = ((float)x - cx) * invx;
            float dy = ((float)y - cy) * invy;
            /* Quadratic-in-radius falloff (illumination-like).  The old
             * quartic r2*r2 confined the effect to the last few corner
             * pixels: at half field it darkened by amount/16. */
            float r2 = 0.5f * (dx * dx + dy * dy);
            float factor = 1.0f - amount * r2;
            size_t i = (y * image->width + x) * 3;
            image->rgb[i] *= factor;
            image->rgb[i + 1] *= factor;
            image->rgb[i + 2] *= factor;
        }
    }
}

static void apply_halation(PjImage *image, float amount, unsigned radius)
{
    if (amount <= 0.0f || radius == 0) return;
    size_t pixels = image->width * image->height;
    float *mask = calloc(pixels * 3, sizeof *mask);
    if (!mask) return;
    for (size_t i = 0; i < pixels; ++i) {
        float r = image->rgb[i * 3];
        float g = image->rgb[i * 3 + 1];
        float b = image->rgb[i * 3 + 2];
        float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        float h = fmaxf(0.0f, y - 0.55f) / 0.45f;
        h = h * h * (3.0f - 2.0f * h);   /* soft threshold */
        mask[i * 3] = h;
        mask[i * 3 + 1] = h;
        mask[i * 3 + 2] = h;
    }
    float diagonal = hypotf((float)image->width, (float)image->height);
    unsigned scaled_radius = (unsigned)lrintf((float)radius * diagonal / 800.0f);
    if (scaled_radius < 1) scaled_radius = 1;
    /* Three box passes approximate a Gaussian and avoid a square halo. */
    box_blur(mask, image->width, image->height, scaled_radius);
    box_blur(mask, image->width, image->height, scaled_radius);
    box_blur(mask, image->width, image->height, scaled_radius);
    for (size_t i = 0; i < pixels; ++i) {
        float h = mask[i * 3] * amount;
        image->rgb[i * 3] += h;
        image->rgb[i * 3 + 1] += h * 0.22f;
        image->rgb[i * 3 + 2] += h * 0.04f;
    }
    free(mask);
}

/*
 * Anchored shoulder: y = (1+r) x^n / (x^n + r) passes through (0,0) and
 * (1,1) for any r, and the exponent n is solved so that it also passes
 * through (pivot, pivot).  Middle gray is therefore preserved exactly and
 * r only shapes how hard the highlights compress (and, symmetrically, how
 * deep the shadows sit).  The previous unanchored form x(1+r)/(x+r) mapped
 * 0.18 to 0.35-0.47, brightening every preset by more than a stop.
 */
static float film_curve(float x, float contrast, float lift, float rolloff)
{
    const float pivot = 0.18f;
    x = fmaxf(x, 0.000001f);
    x = pivot * exp2f(log2f(x / pivot) * contrast);
    float r = fmaxf(rolloff, 0.05f);
    float n = 1.0f + logf(r / (1.0f + r - pivot)) / logf(pivot);
    float xn = powf(x, n);
    x = (1.0f + r) * xn / (xn + r);
    return lift + (1.0f - lift) * x;
}

/*
 * A single RGB image has no depth map, so this is deliberately a photographic
 * proxy rather than a light transport model.  It combines underexposed ambient
 * with a broad frontal beam whose intensity falls away from the optical axis.
 * Screen-like neutral fill flattens nearby tones and permits the hard, slightly
 * cool highlight response associated with a small on-camera xenon flash.
 */
static void apply_direct_flash(PjImage *image, const PjPreset *p, float strength)
{
    if (p->flash_ev <= 0.0f || strength <= 0.0f) return;
    float ambient = exp2f(p->flash_ambient_ev * strength);
    float cx = ((float)image->width - 1.0f) * 0.5f;
    float cy = ((float)image->height - 1.0f) * 0.5f;
    float invx = cx > 0.0f ? 1.0f / cx : 0.0f;
    float invy = cy > 0.0f ? 1.0f / cy : 0.0f;

    for (size_t y = 0; y < image->height; ++y) {
        for (size_t x = 0; x < image->width; ++x) {
            float dx = ((float)x - cx) * invx;
            float dy = ((float)y - cy) * invy;
            float r2 = 0.5f * (dx * dx + dy * dy);
            float beam = 1.0f / (1.0f + p->flash_falloff * r2);
            float gain = ambient * exp2f(p->flash_ev * strength * beam);
            float fill = clamp01(p->flash_fill * strength * beam);
            size_t i = (y * image->width + x) * 3;
            for (size_t c = 0; c < 3; ++c) {
                float cool = c == 2 ? 1.0f + p->flash_cool * strength
                                    : (c == 0 ? 1.0f - 0.35f * p->flash_cool * strength
                                              : 1.0f);
                float base = clamp01(image->rgb[i + c] * gain);
                float flashed = 1.0f - (1.0f - base) * (1.0f - fill * cool);
                image->rgb[i + c] = clamp01(flashed);
            }
        }
    }
}

static void apply_input_balance(PjImage *image, const PjPreset *p, float strength)
{
    float exposure = exp2f(p->exposure_ev * strength);
    float red_balance = exp2f(p->temperature * strength);
    float blue_balance = exp2f(-p->temperature * strength);
    float green_balance = exp2f(p->tint * strength);

    for (size_t i = 0; i < image->width * image->height; ++i) {
        image->rgb[i * 3] *= exposure * red_balance;
        image->rgb[i * 3 + 1] *= exposure * green_balance;
        image->rgb[i * 3 + 2] *= exposure * blue_balance;
    }
}

static void apply_builtin_tone(PjImage *image, const PjPreset *p, float strength)
{
    float contrast = mixf(1.0f, p->contrast, strength);
    float lift = p->black_lift * strength;
    float rolloff = mixf(1.0f, p->highlight_rolloff, strength);
    for (size_t i = 0; i < image->width * image->height * 3; ++i) {
        image->rgb[i] = film_curve(image->rgb[i], contrast, lift, rolloff);
    }
}

static void apply_builtin_color(PjImage *image, const PjPreset *p, float strength)
{
    for (size_t i = 0; i < image->width * image->height; ++i) {
        float in[3];
        for (size_t c = 0; c < 3; ++c)
            in[c] = linear_to_srgb(image->rgb[i * 3 + c]);

        float graded[3] = {0};
        for (size_t row = 0; row < 3; ++row) {
            for (size_t col = 0; col < 3; ++col) {
                float m = mixf(row == col ? 1.0f : 0.0f,
                               p->matrix[row * 3 + col], strength);
                graded[row] += m * in[col];
            }
        }
        float y = 0.2126f * graded[0] +
                  0.7152f * graded[1] +
                  0.0722f * graded[2];
        float saturation = mixf(1.0f, p->saturation, strength);
        if (p->monochrome) saturation = 1.0f - strength;
        float shadow_weight = (1.0f - clamp01(y)) * (1.0f - clamp01(y));
        float highlight_weight = clamp01(y) * clamp01(y);
        for (size_t c = 0; c < 3; ++c) {
            float value = y + saturation * (graded[c] - y);
            float target_gamma = p->channel_gamma[c] > 0.0f
                               ? p->channel_gamma[c] : 1.0f;
            value = powf(clamp01(value), mixf(1.0f, target_gamma, strength));
            value += strength * (p->shadow_tint[c] * shadow_weight +
                                 p->highlight_tint[c] * highlight_weight);
            image->rgb[i * 3 + c] = srgb_to_linear(value);
        }
    }
}

/*
 * Age is a process stage, not a stock property: it must apply whether the
 * color came from the built-in grade or from an external LUT.  Storage fog
 * raises base density with a slight magenta drift (the green-sensitive dye
 * layer degrades fastest in expired C-41), and overall contrast decays.
 * Both are display-referred operations on the developed image.
 */
static void apply_age(PjImage *image, float fade, float strength)
{
    float amount = fade * strength;
    if (amount <= 0.0f) return;
    const float fog_color[3] = {0.58f, 0.53f, 0.57f};
    float fog = 0.18f * amount;
    float contrast = 1.0f - 0.20f * amount;
    for (size_t i = 0; i < image->width * image->height; ++i) {
        for (size_t c = 0; c < 3; ++c) {
            float value = linear_to_srgb(image->rgb[i * 3 + c]);
            value = value * (1.0f - fog) + fog_color[c] * fog;
            value = 0.5f + (value - 0.5f) * contrast;
            image->rgb[i * 3 + c] = srgb_to_linear(clamp01(value));
        }
    }
}

static void apply_color_lut(PjImage *image, const PjLut3D *lut, float strength)
{
    if (!lut) return;
    for (size_t i = 0; i < image->width * image->height; ++i) {
        float original[3];
        float mapped[3];
        for (size_t c = 0; c < 3; ++c) {
            original[c] = linear_to_srgb(image->rgb[i * 3 + c]);
            mapped[c] = original[c];
        }
        pj_lut3d_apply(lut, mapped);
        for (size_t c = 0; c < 3; ++c)
            image->rgb[i * 3 + c] =
                srgb_to_linear(mixf(original[c], mapped[c], strength));
    }
}

static uint64_t hash64(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static float signed_noise(uint64_t seed, size_t x, size_t y, unsigned channel)
{
    uint64_t key = seed ^ ((uint64_t)x * UINT64_C(0x9e3779b185ebca87)) ^
                   ((uint64_t)y * UINT64_C(0xc2b2ae3d27d4eb4f)) ^
                   ((uint64_t)channel * UINT64_C(0x165667b19e3779f9));
    uint64_t bits = hash64(key);
    return (float)((bits >> 40) * (1.0 / 8388607.5) - 1.0);
}

static float smooth_noise(uint64_t seed, float x, float y)
{
    size_t x0 = (size_t)floorf(x), y0 = (size_t)floorf(y);
    float tx = x - (float)x0, ty = y - (float)y0;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float n00 = signed_noise(seed, x0, y0, 0);
    float n10 = signed_noise(seed, x0 + 1, y0, 0);
    float n01 = signed_noise(seed, x0, y0 + 1, 0);
    float n11 = signed_noise(seed, x0 + 1, y0 + 1, 0);
    return mixf(mixf(n00, n10, tx), mixf(n01, n11, tx), ty);
}

static void apply_grain(PjImage *image, float amount, float scale,
                        float midtone_bias, uint64_t seed)
{
    if (amount <= 0.0f) return;
    if (scale < 1.0f) scale = 1.0f;
    for (size_t y = 0; y < image->height; ++y) {
        for (size_t x = 0; x < image->width; ++x) {
            size_t i = (y * image->width + x) * 3;
            float luma = 0.2126f * image->rgb[i] +
                         0.7152f * image->rgb[i + 1] +
                         0.0722f * image->rgb[i + 2];
            luma = clamp01(luma);
            float midtone = 4.0f * luma * (1.0f - luma);
            float density = amount * mixf(1.0f, midtone, clamp01(midtone_bias));
            float noise = smooth_noise(seed, (float)x / scale, (float)y / scale);
            /* Display-referred grain, as in the darktable practice cited in
             * PRESET_SOURCES.md.  Adding the noise in linear light made it
             * nearly invisible at these amplitudes after sRGB encoding. */
            for (unsigned c = 0; c < 3; ++c) {
                float display = linear_to_srgb(image->rgb[i + c]);
                image->rgb[i + c] =
                    srgb_to_linear(clamp01(display + density * noise));
            }
        }
    }
}

static PjImage *square_crop(const PjImage *input, PjError *error)
{
    size_t side = input->width < input->height ? input->width : input->height;
    size_t x0 = (input->width - side) / 2;
    size_t y0 = (input->height - side) / 2;
    PjImage *crop = pj_image_new(side, side, error);
    if (!crop) return NULL;
    for (size_t y = 0; y < side; ++y)
        memcpy(&crop->rgb[y * side * 3],
               &input->rgb[((y + y0) * input->width + x0) * 3],
               side * 3 * sizeof(float));
    return crop;
}

static PjImage *crop_aspect(const PjImage *input, double aspect, PjError *error)
{
    size_t width = input->width;
    size_t height = (size_t)lrint((double)width / aspect);
    if (height > input->height) {
        height = input->height;
        width = (size_t)lrint((double)height * aspect);
    }
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    size_t x0 = (input->width - width) / 2;
    size_t y0 = (input->height - height) / 2;
    PjImage *crop = pj_image_new(width, height, error);
    if (!crop) return NULL;
    for (size_t y = 0; y < height; ++y)
        memcpy(&crop->rgb[y * width * 3],
               &input->rgb[((y + y0) * input->width + x0) * 3],
               width * 3 * sizeof(float));
    return crop;
}

static PjImage *instant_frame(const PjImage *input, PjError *error)
{
    size_t outer_width = (size_t)lrint((double)input->width * 88.47 / 78.94);
    size_t outer_height = (size_t)lrint((double)input->height * 107.52 / 76.80);
    size_t left = (outer_width - input->width) / 2;
    size_t top = left;
    PjImage *framed = pj_image_new(outer_width, outer_height, error);
    if (!framed) return NULL;
    size_t pixels = framed->width * framed->height;
    for (size_t i = 0; i < pixels; ++i) {
        framed->rgb[i * 3] = 0.91f;
        framed->rgb[i * 3 + 1] = 0.89f;
        framed->rgb[i * 3 + 2] = 0.84f;
    }
    for (size_t y = 0; y < input->height; ++y)
        memcpy(&framed->rgb[((y + top) * framed->width + left) * 3],
               &input->rgb[y * input->width * 3],
               input->width * 3 * sizeof(float));
    return framed;
}

PjImage *pj_render(const PjImage *input, const char *preset_name,
                   const PjRenderOptions *options, PjError *error)
{
    if (!input || !input->rgb) {
        pj_set_error(error, "no input image");
        return NULL;
    }
    const PjPreset *preset = pj_find_preset(preset_name);
    if (!preset) {
        pj_set_error(error, "unknown preset '%s'", preset_name ? preset_name : "");
        return NULL;
    }
    float strength = options ? options->strength : 1.0f;
    uint64_t seed = options ? options->seed : UINT64_C(0x504f4c414a554943);
    const PjLut3D *lut = options ? options->color_lut : NULL;
    strength = clamp01(strength);

    PjImage *image;
    if (preset->instant_frame)
        image = crop_aspect(input, 78.94 / 76.80, error);
    else if (preset->square_crop)
        image = square_crop(input, error);
    else if (preset->crop_aspect > 0.0f)
        image = crop_aspect(input, (double)preset->crop_aspect, error);
    else
        image = clone_image(input, error);
    if (!image) return NULL;

    apply_direct_flash(image, preset, strength);
    apply_softness(image, preset->softness * strength);
    apply_vignette(image, preset->vignette * strength);
    apply_halation(image, preset->halation * strength,
                   preset->halation_radius);
    apply_input_balance(image, preset, strength);
    if (lut)
        apply_color_lut(image, lut, strength);
    else {
        apply_builtin_tone(image, preset, strength);
        apply_builtin_color(image, preset, strength);
    }
    apply_age(image, preset->fade, strength);
    apply_grain(image, preset->grain * strength, preset->grain_scale,
                preset->grain_midtone_bias, seed);

    if (preset->instant_frame) {
        PjImage *framed = instant_frame(image, error);
        pj_image_free(image);
        return framed;
    }
    return image;
}
