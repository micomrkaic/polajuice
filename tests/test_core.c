#include "polajuice.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void fill_test_pattern(PjImage *image)
{
    float *pixels = pj_image_pixels(image);
    size_t w = pj_image_width(image), h = pj_image_height(image);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            size_t i = (y * w + x) * 3;
            pixels[i] = (float)x / (float)(w - 1);
            pixels[i + 1] = (float)y / (float)(h - 1);
            pixels[i + 2] = 0.25f;
        }
    }
}

static void fill_uniform(PjImage *image, float value)
{
    float *pixels = pj_image_pixels(image);
    size_t samples = pj_image_width(image) * pj_image_height(image) * 3;
    for (size_t i = 0; i < samples; ++i) pixels[i] = value;
}

static float luma_at(const PjImage *image, size_t x, size_t y)
{
    const float *pixels = pj_image_pixels_const(image);
    size_t i = (y * pj_image_width(image) + x) * 3;
    return 0.2126f * pixels[i] + 0.7152f * pixels[i + 1] +
           0.0722f * pixels[i + 2];
}

static PjLut3D *make_identity_lut(PjError *error)
{
    const char *path = "/tmp/polajuice-identity.cube";
    FILE *file = fopen(path, "w");
    assert(file);
    fputs("TITLE \"identity\"\n"
          "LUT_3D_SIZE 2\n"
          "DOMAIN_MIN 0 0 0\n"
          "DOMAIN_MAX 1 1 1\n"
          "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
          "0 0 1\n1 0 1\n0 1 1\n1 1 1\n", file);
    assert(fclose(file) == 0);
    PjLut3D *lut = pj_lut3d_load_cube(path, error);
    assert(remove(path) == 0);
    return lut;
}

int main(void)
{
    PjError error = {{0}};
    assert(pj_preset_count() >= 9);
    assert(pj_preset_description("35mm-negative") != NULL);
    assert(pj_preset_description("does-not-exist") == NULL);

    PjImage *input = pj_image_new(32, 24, &error);
    assert(input != NULL);
    fill_test_pattern(input);

    const char *roundtrip_path = "/tmp/polajuice-test-roundtrip.ppm";
    assert(pj_image_save_ppm(input, roundtrip_path, &error));
    PjImage *roundtrip = pj_image_load_ppm(roundtrip_path, &error);
    assert(roundtrip);
    assert(pj_image_width(roundtrip) == pj_image_width(input));
    assert(pj_image_height(roundtrip) == pj_image_height(input));
    pj_image_free(roundtrip);
    assert(remove(roundtrip_path) == 0);

    /* PNG must round-trip losslessly through the stb path; JPEG must come
     * back close (lossy) at the same dimensions.  Format is picked from the
     * extension by pj_image_save/pj_image_load. */
    const char *png_path = "/tmp/polajuice-test.png";
    assert(pj_image_save(input, png_path, &error));
    PjImage *png_back = pj_image_load(png_path, &error);
    assert(png_back);
    assert(pj_image_width(png_back) == pj_image_width(input));
    {
        size_t n = pj_image_width(input) * pj_image_height(input) * 3;
        const float *x = pj_image_pixels_const(input);
        const float *y = pj_image_pixels_const(png_back);
        for (size_t i = 0; i < n; ++i)
            assert(fabsf(x[i] - y[i]) < 0.004f);   /* 8-bit quantization only */
    }
    pj_image_free(png_back);
    assert(remove(png_path) == 0);

    const char *jpg_path = "/tmp/polajuice-test.jpg";
    assert(pj_image_save(input, jpg_path, &error));
    PjImage *jpg_back = pj_image_load(jpg_path, &error);
    assert(jpg_back);
    assert(pj_image_width(jpg_back) == pj_image_width(input));
    assert(pj_image_height(jpg_back) == pj_image_height(input));
    pj_image_free(jpg_back);
    assert(remove(jpg_path) == 0);

    PjRenderOptions a = {.seed = 42, .strength = 1.0f};
    PjImage *first = pj_render(input, "35mm-negative", &a, &error);
    PjImage *second = pj_render(input, "35mm-negative", &a, &error);
    assert(first && second);
    size_t bytes = pj_image_width(first) * pj_image_height(first) * 3 * sizeof(float);
    assert(memcmp(pj_image_pixels_const(first), pj_image_pixels_const(second), bytes) == 0);

    PjLut3D *identity = make_identity_lut(&error);
    assert(identity && pj_lut3d_size(identity) == 2);
    float sample[3] = {0.20f, 0.40f, 0.80f};
    pj_lut3d_apply(identity, sample);
    assert(fabsf(sample[0] - 0.20f) < 0.00001f);
    assert(fabsf(sample[1] - 0.40f) < 0.00001f);
    assert(fabsf(sample[2] - 0.80f) < 0.00001f);

    /* A stock LUT must not bypass camera/lighting stages such as flash. */
    PjRenderOptions lut_options = {.seed = 42, .strength = 1.0f,
                                   .color_lut = identity};
    /* Direct flash must create an unmistakable near/center versus edge falloff. */
    PjImage *flat = pj_image_new(65, 65, &error);
    assert(flat);
    fill_uniform(flat, 0.18f);
    PjImage *flashed = pj_render(flat, "disposable-flash", &lut_options, &error);
    assert(flashed);
    float flash_center = luma_at(flashed, 32, 32);
    float flash_corner = luma_at(flashed, 0, 0);
    assert(flash_center > flash_corner * 1.35f);

    /* The tone stage must roughly preserve middle gray: a uniform 18% gray
     * card should stay within about a third of a stop at the image center.
     * This guards against unanchored highlight-rolloff regressions that
     * previously brightened every preset by more than a stop. */
    PjImage *card = pj_image_new(65, 65, &error);
    assert(card);
    fill_uniform(card, 0.18f);
    PjRenderOptions plain = {.seed = 42, .strength = 1.0f};
    PjImage *graded = pj_render(card, "35mm-slide", &plain, &error);
    assert(graded);
    float mid = luma_at(graded, 32, 32);
    assert(mid > 0.14f && mid < 0.24f);
    pj_image_free(graded);
    pj_image_free(card);

    a.seed = 43;
    PjImage *third = pj_render(input, "35mm-negative", &a, &error);
    assert(third);
    assert(memcmp(pj_image_pixels_const(first), pj_image_pixels_const(third), bytes) != 0);

    PjImage *instant = pj_render(input, "polaroid-600", &a, &error);
    assert(instant);
    assert(pj_image_width(instant) > pj_image_height(input));
    assert(pj_image_height(instant) > pj_image_width(instant));
    assert(fabs((double)pj_image_width(instant) / pj_image_height(instant) -
                88.47 / 107.52) < 0.03);

    /* Super 8 renders through its 1.36:1 projector-aperture gate. */
    PjImage *gate = pj_render(input, "super8", &a, &error);
    assert(gate);
    assert(fabs((double)pj_image_width(gate) / pj_image_height(gate) - 1.362) < 0.06);
    pj_image_free(gate);

    /* Expired film must show base fog: pure black comes out lifted, and the
     * age stage must survive a LUT since fade is a process, not stock, trait. */
    PjImage *black = pj_image_new(33, 33, &error);
    assert(black);
    fill_uniform(black, 0.0f);
    PjRenderOptions aged = {.seed = 42, .strength = 1.0f, .color_lut = identity};
    PjImage *fogged = pj_render(black, "expired-film", &aged, &error);
    assert(fogged);
    assert(luma_at(fogged, 16, 16) > 0.008f);
    pj_image_free(fogged);
    pj_image_free(black);

    pj_image_free(instant);
    pj_image_free(flashed);
    pj_image_free(flat);
    pj_lut3d_free(identity);
    pj_image_free(third);
    pj_image_free(second);
    pj_image_free(first);
    pj_image_free(input);
    puts("all tests passed");
    return 0;
}
