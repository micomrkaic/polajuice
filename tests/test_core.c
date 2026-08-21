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
    assert(pj_preset_count() >= 13);
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

    /* Camera archetypes declare their canonical film. */
    assert(pj_preset_default_film("polaroid-600"));
    assert(!strcmp(pj_preset_default_film("polaroid-600"), "polaroid_px-680"));
    assert(pj_preset_default_film("no-such-camera") == NULL);

    /* Temporal axis: frames of a reel differ (weave + grain), the same
     * frame is deterministic, and stills (temporal=false) are unaffected
     * by the frame field. */
    PjImage *mcard = pj_image_new(65, 65, &error);
    assert(mcard);
    fill_uniform(mcard, 0.35f);
    PjRenderOptions m0 = {.seed = 5, .strength = 1.0f,
                          .temporal = true, .frame = 0};
    PjRenderOptions m7 = {.seed = 5, .strength = 1.0f,
                          .temporal = true, .frame = 7};
    PjImage *f0 = pj_render(mcard, "super8", &m0, &error);
    PjImage *f7 = pj_render(mcard, "super8", &m7, &error);
    PjImage *f0b = pj_render(mcard, "super8", &m0, &error);
    assert(f0 && f7 && f0b);
    {
        const float *a = pj_image_pixels_const(f0);
        const float *b = pj_image_pixels_const(f7);
        const float *c = pj_image_pixels_const(f0b);
        size_t n = pj_image_width(f0) * pj_image_height(f0) * 3;
        float diff = 0.0f;
        bool identical = true;
        for (size_t i = 0; i < n; ++i) {
            diff += fabsf(a[i] - b[i]);
            if (a[i] != c[i]) identical = false;
        }
        assert(diff / (float)n > 0.001f);   /* frames visibly differ */
        assert(identical);                  /* same frame is deterministic */
    }
    pj_image_free(f0); pj_image_free(f7); pj_image_free(f0b);
    PjRenderOptions still_a = {.seed = 5, .strength = 1.0f, .frame = 0};
    PjRenderOptions still_b = {.seed = 5, .strength = 1.0f, .frame = 99};
    PjImage *sa = pj_render(mcard, "super8", &still_a, &error);
    PjImage *sb = pj_render(mcard, "super8", &still_b, &error);
    assert(sa && sb);
    {
        const float *a = pj_image_pixels_const(sa);
        const float *b = pj_image_pixels_const(sb);
        size_t n = pj_image_width(sa) * pj_image_height(sa) * 3;
        for (size_t i = 0; i < n; ++i) assert(a[i] == b[i]);
    }
    pj_image_free(sa); pj_image_free(sb);
    pj_image_free(mcard);

    /* Film-process compatibility truth table. */
    assert(pj_preset_accepts_film("super8", "slide"));
    assert(!pj_preset_accepts_film("super8", "negative"));
    assert(pj_preset_accepts_film("35mm-negative", "negative"));
    assert(!pj_preset_accepts_film("35mm-negative", "slide"));
    assert(pj_preset_accepts_film("polaroid-600", "integral"));
    assert(!pj_preset_accepts_film("polaroid-600", "pack"));
    assert(pj_preset_accepts_film("polaroid-packfilm", "pack"));
    assert(!pj_preset_accepts_film("autochrome", "slide"));
    assert(!pj_preset_accepts_film("technicolor-3strip", "negative"));
    assert(pj_preset_accepts_film("midcentury-rangefinder", "bw"));
    char procs[256];
    assert(pj_preset_film_processes("autochrome", procs, sizeof procs));
    assert(procs[0] == '\0');   /* sealed */

    /* Traits are generated from the preset's own parameters. */
    char traits[512];
    assert(pj_preset_traits("polaroid-600", traits, sizeof traits));
    assert(strstr(traits, "instant-print"));
    assert(pj_preset_traits("disposable-flash", traits, sizeof traits));
    assert(strstr(traits, "flash"));
    assert(pj_preset_traits("nope", traits, sizeof traits) == NULL);

    /* Downscale: long edge bounded, aspect kept, small images untouched. */
    PjImage *big = pj_image_new(400, 300, &error);
    assert(big);
    fill_uniform(big, 0.4f);
    big = pj_image_downscale(big, 100, &error);
    assert(big && pj_image_width(big) == 100 && pj_image_height(big) == 75);
    PjImage *same = pj_image_downscale(big, 500, &error);
    assert(same == big);
    pj_image_free(big);

    /* Autochrome's colored grain: per-channel noise must decorrelate the
     * channels on a gray card, unlike luminance grain which keeps R==G==B. */
    PjImage *card2 = pj_image_new(65, 65, &error);
    assert(card2);
    fill_uniform(card2, 0.4f);
    PjRenderOptions g = {.seed = 9, .strength = 1.0f};
    PjImage *pointillist = pj_render(card2, "autochrome", &g, &error);
    assert(pointillist);
    {
        const float *px = pj_image_pixels_const(pointillist);
        size_t w = pj_image_width(pointillist);
        float maxdiff = 0.0f;
        for (size_t k = 0; k < 200; ++k) {
            size_t i = ((10 + k / 20) * w + 10 + k % 20) * 3;
            float d = fabsf(px[i] - px[i + 1]);
            if (d > maxdiff) maxdiff = d;
        }
        assert(maxdiff > 0.01f);   /* channels visibly decorrelated */
    }
    pj_image_free(pointillist);
    pj_image_free(card2);

    /* Pack film frames to its own peel-apart geometry, not the 600 chin. */
    PjImage *pack_in = pj_image_new(365, 476, &error);
    assert(pack_in);
    fill_uniform(pack_in, 0.3f);
    PjImage *pack = pj_render(pack_in, "polaroid-packfilm", &g, &error);
    assert(pack);
    {
        double ratio = (double)pj_image_width(pack) / pj_image_height(pack);
        assert(fabs(ratio - 83.0 / 108.0) < 0.03);
    }
    pj_image_free(pack);
    pj_image_free(pack_in);

    /* Technicolor renders through the Academy gate. */
    PjImage *acad_in = pj_image_new(200, 200, &error);
    assert(acad_in);
    fill_uniform(acad_in, 0.3f);
    PjImage *acad = pj_render(acad_in, "technicolor-3strip", &g, &error);
    assert(acad);
    assert(fabs((double)pj_image_width(acad) / pj_image_height(acad) - 1.375)
           < 0.05);
    pj_image_free(acad);
    pj_image_free(acad_in);

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

    /* The age axis must show base fog on any camera: pure black comes out
     * lifted, and aging must survive a LUT since it is a process trait. */
    PjImage *black = pj_image_new(33, 33, &error);
    assert(black);
    fill_uniform(black, 0.0f);
    PjRenderOptions aged = {.seed = 42, .strength = 1.0f, .age = 0.65f,
                            .color_lut = identity};
    PjImage *fogged = pj_render(black, "35mm-negative", &aged, &error);
    assert(fogged);
    assert(luma_at(fogged, 16, 16) > 0.008f);
    pj_image_free(fogged);
    /* age 0 must leave black essentially black */
    PjRenderOptions fresh = {.seed = 42, .strength = 1.0f, .color_lut = identity};
    PjImage *unaged = pj_render(black, "35mm-negative", &fresh, &error);
    assert(unaged);
    assert(luma_at(unaged, 16, 16) < 0.004f);
    pj_image_free(unaged);
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
