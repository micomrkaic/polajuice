#include "polajuice.h"
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
    assert(pj_preset_count() >= 14);
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
    /* motion_scale must change the temporal render (weave amplitude) */
    PjRenderOptions m3 = m0;
    m3.motion_scale = 3.0f;
    PjImage *f0s = pj_render(mcard, "super8", &m3, &error);
    assert(f0s);
    {
        const float *a = pj_image_pixels_const(f0);
        const float *b = pj_image_pixels_const(f0s);
        size_t n = pj_image_width(f0) * pj_image_height(f0) * 3;
        bool differs = false;
        for (size_t i = 0; i < n; ++i)
            if (a[i] != b[i]) { differs = true; break; }
        assert(differs);
    }
    pj_image_free(f0s);
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

    PjLut3D *identity = make_identity_lut(&error);
    assert(identity);

    /* Tetrahedral interpolation: identity LUT must be exact at lattice
     * points and along the neutral axis (where tetrahedral shines). */
    {
        float probe[3] = {0.3f, 0.3f, 0.3f};
        pj_lut3d_apply(identity, probe);
        assert(fabsf(probe[0] - 0.3f) < 1e-5f);
        assert(fabsf(probe[1] - 0.3f) < 1e-5f);
        assert(fabsf(probe[2] - 0.3f) < 1e-5f);
        float asym[3] = {0.7f, 0.2f, 0.5f};
        pj_lut3d_apply(identity, asym);
        assert(fabsf(asym[0] - 0.7f) < 1e-5f &&
               fabsf(asym[1] - 0.2f) < 1e-5f &&
               fabsf(asym[2] - 0.5f) < 1e-5f);
    }

    /* Age v2: per-process dye fade. Aged slide drifts red-magenta (cyan
     * dye dies), aged B&W stays neutral (silver does not fade), and both
     * lift the base. */
    {
        PjImage *gray = pj_image_new(33, 33, &error);
        assert(gray);
        fill_uniform(gray, 0.4f);
        /* compare aged against fresh through the identical pipeline so
         * camera tints cannot confound the fade direction */
        PjRenderOptions fresh = {.seed = 3, .strength = 1.0f,
            .color_lut = identity, .film_process = "slide"};
        PjRenderOptions slide_aged = fresh;
        slide_aged.age = 0.8f;
        PjImage *f0 = pj_render(gray, "35mm-slide", &fresh, &error);
        PjImage *sl = pj_render(gray, "35mm-slide", &slide_aged, &error);
        assert(f0 && sl);
        float dmean[3] = {0, 0, 0};
        const float *pf = pj_image_pixels_const(f0);
        const float *px = pj_image_pixels_const(sl);
        size_t n = pj_image_width(sl) * pj_image_height(sl);
        for (size_t i = 0; i < n; ++i)
            for (size_t c = 0; c < 3; ++c)
                dmean[c] += px[i * 3 + c] - pf[i * 3 + c];
        for (size_t c = 0; c < 3; ++c) dmean[c] /= (float)n;
        /* cyan dye death: red must gain more than green and blue */
        assert(dmean[0] > dmean[1] + 0.005f);
        assert(dmean[0] > dmean[2] + 0.005f);
        pj_image_free(f0);
        pj_image_free(sl);

        PjRenderOptions bw_aged = {.seed = 3, .strength = 1.0f,
            .age = 0.8f, .film_process = "bw"};
        PjImage *bwout = pj_render(gray, "bw-35", &bw_aged, &error);
        assert(bwout);
        px = pj_image_pixels_const(bwout);
        float spread = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            float hi = px[i*3], lo = px[i*3];
            for (size_t c = 1; c < 3; ++c) {
                if (px[i*3+c] > hi) hi = px[i*3+c];
                if (px[i*3+c] < lo) lo = px[i*3+c];
            }
            spread += hi - lo;
        }
        assert(spread / (float)n < 0.02f);   /* essentially neutral */
        pj_image_free(bwout);
        pj_image_free(gray);
    }

    /* Print LUT chains after the film LUT: a channel-swapping print cube
     * must change the result of an identity-film render. */
    {
        FILE *pf = fopen("/tmp/pj_test_print.cube", "w");
        assert(pf);
        /* swap red and green: lattice ordered r-fastest */
        fputs("LUT_3D_SIZE 2\n0 0 0\n0 1 0\n1 0 0\n1 1 0\n"
              "0 0 1\n0 1 1\n1 0 1\n1 1 1\n", pf);
        fclose(pf);
        PjLut3D *print = pj_lut3d_load_cube("/tmp/pj_test_print.cube", &error);
        assert(print);
        PjImage *card3 = pj_image_new(17, 17, &error);
        assert(card3);
        float *cpx = pj_image_pixels(card3);
        for (size_t i = 0; i < 17 * 17; ++i) {
            cpx[i * 3] = 0.6f; cpx[i * 3 + 1] = 0.2f; cpx[i * 3 + 2] = 0.2f;
        }
        PjRenderOptions plain = {.seed = 1, .strength = 1.0f,
                                 .color_lut = identity};
        PjRenderOptions printed = {.seed = 1, .strength = 1.0f,
                                   .color_lut = identity, .print_lut = print};
        PjImage *a2 = pj_render(card3, "35mm-negative", &plain, &error);
        PjImage *b2 = pj_render(card3, "35mm-negative", &printed, &error);
        assert(a2 && b2);
        const float *pa = pj_image_pixels_const(a2);
        const float *pb = pj_image_pixels_const(b2);
        /* the reddish card must come out green-dominant through the swap */
        assert(pa[3 * (8 * 17 + 8)] > pa[3 * (8 * 17 + 8) + 1]);
        assert(pb[3 * (8 * 17 + 8) + 1] > pb[3 * (8 * 17 + 8)]);
        pj_image_free(a2); pj_image_free(b2); pj_image_free(card3);
        pj_lut3d_free(print);
        remove("/tmp/pj_test_print.cube");
    }

    /* Grain character from the film stem: speed orders variance, tabular
     * runs finer than cubic at equal speed, no stem changes nothing. */
    {
        PjImage *flat = pj_image_new(97, 97, &error);
        assert(flat);
        fill_uniform(flat, 0.42f);
        float variance[4];
        const char *stems[4] = {NULL, "ilford_pan_f_plus_50",
                                "ilford_hp_5_plus_400", "ilford_delta_3200"};
        for (int k = 0; k < 4; ++k) {
            PjRenderOptions go = {.seed = 9, .strength = 1.0f,
                                  .color_lut = identity,
                                  .film_process = "bw",
                                  .film_stem = stems[k]};
            PjImage *out = pj_render(flat, "bw-35", &go, &error);
            assert(out);
            const float *px = pj_image_pixels_const(out);
            size_t n = pj_image_width(out) * pj_image_height(out);
            double mean = 0, var = 0;
            for (size_t i = 0; i < n; ++i) mean += px[i * 3];
            mean /= (double)n;
            for (size_t i = 0; i < n; ++i)
                var += (px[i * 3] - mean) * (px[i * 3] - mean);
            variance[k] = (float)(var / (double)n);
            pj_image_free(out);
        }
        assert(variance[1] < variance[2]);          /* Pan F < HP5 */
        assert(variance[2] < variance[3]);          /* HP5 < Delta 3200 */
        /* tabular vs cubic at 400: T-Max finer than Tri-X */
        float tv[2];
        const char *pair[2] = {"kodak_tri-x_400", "kodak_t-max_400"};
        for (int k = 0; k < 2; ++k) {
            PjRenderOptions go = {.seed = 9, .strength = 1.0f,
                                  .color_lut = identity,
                                  .film_process = "bw", .film_stem = pair[k]};
            PjImage *out = pj_render(flat, "bw-35", &go, &error);
            assert(out);
            const float *px = pj_image_pixels_const(out);
            size_t n = pj_image_width(out) * pj_image_height(out);
            double mean = 0, var = 0;
            for (size_t i = 0; i < n; ++i) mean += px[i * 3];
            mean /= (double)n;
            for (size_t i = 0; i < n; ++i)
                var += (px[i * 3] - mean) * (px[i * 3] - mean);
            tv[k] = (float)(var / (double)n);
            pj_image_free(out);
        }
        assert(tv[1] < tv[0]);
        pj_image_free(flat);
    }

    /* Contrast filters: red darkens a blue sky through bw far more than
     * yellow; "none" and NULL agree; unknown names are rejected by the
     * front-end helper. */
    {
        PjImage *sky = pj_image_new(48, 48, &error);
        assert(sky);
        float *px = pj_image_pixels(sky);
        for (size_t i = 0; i < 48 * 48; ++i) {
            px[i * 3 + 0] = 0.20f; px[i * 3 + 1] = 0.35f; px[i * 3 + 2] = 0.85f;
        }
        double mean[3];
        const char *racks[3] = {NULL, "yellow", "red"};
        for (int k = 0; k < 3; ++k) {
            PjRenderOptions fo = {.seed = 4, .strength = 1.0f,
                                  .color_lut = identity,
                                  .film_process = "bw",
                                  .contrast_filter = racks[k]};
            PjImage *out = pj_render(sky, "bw-35", &fo, &error);
            assert(out);
            const float *q = pj_image_pixels_const(out);
            size_t n = pj_image_width(out) * pj_image_height(out);
            double m = 0;
            for (size_t i = 0; i < n; ++i)
                m += 0.2126 * q[i * 3] + 0.7152 * q[i * 3 + 1]
                   + 0.0722 * q[i * 3 + 2];
            mean[k] = m / (double)n;
            pj_image_free(out);
        }
        assert(mean[1] < mean[0]);      /* yellow darkens blue sky */
        assert(mean[2] < mean[1]);      /* red darkens it far more */
        assert(pj_contrast_filter_known("orange"));
        assert(pj_contrast_filter_known(NULL));
        assert(pj_contrast_filter_known("none"));
        assert(!pj_contrast_filter_known("sepia"));
        pj_image_free(sky);
    }

    /* Silver grain: texture follows exposure. On the same stock a dark
     * field is carried by the coarse population and a bright one by the
     * fine, so the high-passed noise correlates more between neighbors in
     * the shadows than in the highlights. Classic grain has no such
     * dependence. Deterministic per seed, and a uniform field still
     * shows grain (the image is made of grains, not overlaid). */
    {
        const char *models[2] = {"classic", "silver"};
        float corr[2][2];   /* [model][dark, bright] */
        for (int m = 0; m < 2; ++m)
            for (int b = 0; b < 2; ++b) {
                PjImage *field = pj_image_new(96, 96, &error);
                assert(field);
                fill_uniform(field, b ? 0.75f : 0.06f);
                PjRenderOptions go = {.seed = 11, .strength = 1.0f,
                                      .color_lut = identity,
                                      .film_process = "bw",
                                      .grain_model = models[m]};
                PjImage *out = pj_render(field, "bw-35", &go, &error);
                assert(out);
                PjImage *again = pj_render(field, "bw-35", &go, &error);
                assert(again);
                assert(memcmp(pj_image_pixels_const(out),
                              pj_image_pixels_const(again),
                              96 * 96 * 3 * sizeof(float)) == 0);
                pj_image_free(again);
                /* high-pass with a 5x5 box removes the vignette; lag-1
                 * correlation of the residual measures grain size */
                const float *q = pj_image_pixels_const(out);
                static float hp[96 * 96];
                for (size_t y = 2; y < 94; ++y)
                    for (size_t x = 2; x < 94; ++x) {
                        float s = 0.0f;
                        for (int dy = -2; dy <= 2; ++dy)
                            for (int dx = -2; dx <= 2; ++dx)
                                s += q[((y + (size_t)dy) * 96 + x + (size_t)dx) * 3];
                        hp[y * 96 + x] = q[(y * 96 + x) * 3] - s / 25.0f;
                    }
                double v = 0, c = 0;
                for (size_t y = 24; y < 72; ++y)
                    for (size_t x = 24; x < 72; ++x) {
                        v += (double)hp[y * 96 + x] * hp[y * 96 + x];
                        c += (double)hp[y * 96 + x] * hp[y * 96 + x + 1];
                    }
                assert(v > 1e-9);               /* grain present */
                corr[m][b] = (float)(c / v);
                pj_image_free(out);
                pj_image_free(field);
            }
        assert(corr[1][0] > corr[1][1] + 0.05f);    /* silver: shadows coarser */
        assert(fabsf(corr[0][0] - corr[0][1]) < 0.05f); /* classic: no such trend */
        assert(pj_grain_model_known(NULL));
        assert(pj_grain_model_known("classic"));
        assert(pj_grain_model_known("silver"));
        assert(!pj_grain_model_known("bromide"));
    }

    /* Development edge effects: a uniform field maps to itself exactly;
     * a small dense detail on a thin field develops denser than without
     * (Eberhard), while the thin field far from it is untouched. */
    {
        PjImage *flat2 = pj_image_new(49, 49, &error);
        assert(flat2);
        fill_uniform(flat2, 0.30f);
        PjRenderOptions e0 = {.seed = 5, .strength = 1.0f,
                              .color_lut = identity, .film_process = "bw"};
        PjRenderOptions e2 = e0;
        e2.edge = 2.0f;
        PjImage *u0 = pj_render(flat2, "bw-35", &e0, &error);
        PjImage *u2 = pj_render(flat2, "bw-35", &e2, &error);
        assert(u0 && u2);
        {   /* the camera's vignette is the only gradient: negligible */
            const float *a0 = pj_image_pixels_const(u0);
            const float *a2 = pj_image_pixels_const(u2);
            float worst = 0.0f;
            for (size_t i = 0; i < 49 * 49 * 3; ++i)
                worst = fmaxf(worst, fabsf(a0[i] - a2[i]));
            assert(worst < 2e-3f);
        }
        pj_image_free(u0);
        pj_image_free(u2);

        fill_uniform(flat2, 0.03f);
        float *fp = pj_image_pixels(flat2);
        for (size_t y = 21; y < 28; ++y)
            for (size_t x = 21; x < 28; ++x)
                for (size_t c = 0; c < 3; ++c) fp[(y * 49 + x) * 3 + c] = 0.85f;
        PjImage *d0 = pj_render(flat2, "bw-35", &e0, &error);
        PjImage *d2 = pj_render(flat2, "bw-35", &e2, &error);
        assert(d0 && d2);
        double sq0 = 0, sq2 = 0, far0 = 0, far2 = 0;
        for (size_t y = 22; y < 27; ++y)
            for (size_t x = 22; x < 27; ++x) {
                sq0 += luma_at(d0, x, y);
                sq2 += luma_at(d2, x, y);
            }
        for (size_t y = 2; y < 6; ++y)
            for (size_t x = 2; x < 6; ++x) {
                far0 += luma_at(d0, x, y);
                far2 += luma_at(d2, x, y);
            }
        assert(sq2 > sq0 * 1.02);               /* dense detail gains */
        assert(fabs(far2 - far0) < 1e-4);        /* distant field unchanged */
        pj_image_free(d0);
        pj_image_free(d2);
        pj_image_free(flat2);
    }

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
    assert(!strcmp(pj_preset_primary_process("35mm-slide"), "slide"));
    assert(!strcmp(pj_preset_primary_process("polaroid-600"), "integral"));
    assert(pj_preset_primary_process("midcentury-rangefinder") == NULL);
    assert(pj_preset_primary_process("autochrome") == NULL);
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

    /* The scene-linear door. Five synthetic files of one scene (gray
     * card 0.18, red and blue patches, an exactly-white patch, a ramp
     * that clips) through every decoder: uncompressed and lossless-JPEG
     * Bayer DNG, LinearRaw DNG, Nikon-compressed NEF, and a 16-bit PNG
     * through --linear. The DNGs declare a color matrix whose composite
     * with the decoder's recipe is the identity, so decoded pixels must
     * equal the scene; the NEF goes through the D610 table, so only its
     * neutrals are checked. */
    {
        const char *files[4] = {"tests/raw/bayer_u16.dng", "tests/raw/bayer_lj92.dng",
                                "tests/raw/linear_lj92.dng", "tests/raw/lossless14.nef"};
        PjImage *decoded[5] = {0};
        for (int f = 0; f < 4; ++f) {
            decoded[f] = pj_image_load(files[f], &error);
            if (!decoded[f]) { fprintf(stderr, "%s: %s\n", files[f], error.message); }
            assert(decoded[f]);
            assert(pj_image_is_scene_linear(decoded[f]));
            assert(pj_image_width(decoded[f]) == 64 && pj_image_height(decoded[f]) == 48);
        }
        decoded[4] = pj_image_load_linear("tests/raw/linear16.png", &error);
        assert(decoded[4] && pj_image_is_scene_linear(decoded[4]));
        for (int f = 0; f < 5; ++f) {
            const float *px = pj_image_pixels_const(decoded[f]);
            size_t w = 64;
            const float *gray = px + (24 * w + 30) * 3;
            const float *white = px + (8 * w + 57) * 3;
            const float *red = px + (38 * w + 14) * 3;
            const float *ramp = px + (2 * w + 10) * 3;
            for (int c = 0; c < 3; ++c) {
                assert(fabsf(gray[c] - 0.18f) < 0.004f);
                assert(fabsf(white[c] - 1.0f) < 0.004f);
                assert(fabsf(ramp[c] - 0.476f) < 0.006f);
            }
            if (f != 3) {   /* not the NEF: full color round trip */
                assert(fabsf(red[0] - 0.60f) < 0.004f);
                assert(fabsf(red[1] - 0.05f) < 0.004f);
                assert(fabsf(red[2] - 0.05f) < 0.004f);
            } else {
                assert(red[0] > 0.5f && red[1] < 0.1f && red[2] < 0.1f);
            }
            /* nothing above sensor white comes out of the door (the NEF's
             * camera matrix can push a saturated color past 1.0, which is
             * gamut, not headroom; neutrals are checked above) */
            if (f != 3) {
                float peak = 0.0f;
                for (size_t i = 0; i < 64 * 48 * 3; ++i) peak = fmaxf(peak, px[i]);
                assert(peak <= 1.0001f);
            }
        }
        /* uncompressed and lossless-JPEG Bayer agree to the bit (same
         * samples, same demosaic); LinearRaw differs only at patch edges,
         * where there is no demosaic to blur them */
        assert(memcmp(pj_image_pixels_const(decoded[0]), pj_image_pixels_const(decoded[1]),
                      64 * 48 * 3 * sizeof(float)) == 0);
        /* the image area: the same pictures inside a larger sensor
         * readout with a black border - a DNG with ActiveArea at an odd
         * offset (the CFA phase shifts), a NEF with CropHiSpeed in the
         * MakerNote - decode to the bit as the plain files */
        {
            PjImage *bd = pj_image_load("tests/raw/bayer_u16_border.dng", &error);
            if (!bd) fprintf(stderr, "border dng: %s\n", error.message);
            assert(bd && pj_image_width(bd) == 64 && pj_image_height(bd) == 48);
            assert(memcmp(pj_image_pixels_const(bd), pj_image_pixels_const(decoded[0]),
                          64 * 48 * 3 * sizeof(float)) == 0);
            pj_image_free(bd);
            PjImage *bn = pj_image_load("tests/raw/lossless14_border.nef", &error);
            if (!bn) fprintf(stderr, "border nef: %s\n", error.message);
            assert(bn && pj_image_width(bn) == 64 && pj_image_height(bn) == 48);
            assert(memcmp(pj_image_pixels_const(bn), pj_image_pixels_const(decoded[3]),
                          64 * 48 * 3 * sizeof(float)) == 0);
            pj_image_free(bn);
            /* the D610 case: the tag declares no crop, so the image area
             * comes from the table and the side of the masked band from
             * the samples (the darker band is the mask) */
            {
                size_t iw = 0, ih = 0;
                assert(pj_raw_nikon_image_area("NIKON D610", 6080, 4028, &iw, &ih));
                assert(iw == 6034 && ih == 4028);
                assert(!pj_raw_nikon_image_area("NIKON D610", 6016, 4016, &iw, &ih));
                assert(!pj_raw_nikon_image_area("NIKON D5", 6080, 4028, &iw, &ih));
                /* a 40x30 readout, 32x24 picture: masked right and bottom */
                uint16_t buf[40 * 30];
                const double blk[4] = {100, 100, 100, 100};
                for (size_t y = 0; y < 30; ++y)
                    for (size_t x = 0; x < 40; ++x)
                        buf[y * 40 + x] = (uint16_t)((x < 32 && y < 24) ? 100 + 50 + (x * 7 + y * 3) % 40 : 100 + (x + y) % 3);
                size_t x0 = 9, y0 = 9;
                pj_raw_choose_sides(buf, 40, 30, blk, 32, 24, &x0, &y0);
                assert(x0 == 0 && y0 == 0);
                /* masked left and top instead */
                for (size_t y = 0; y < 30; ++y)
                    for (size_t x = 0; x < 40; ++x)
                        buf[y * 40 + x] = (uint16_t)((x >= 8 && y >= 6) ? 100 + 50 + (x * 7 + y * 3) % 40 : 100 + (x + y) % 3);
                pj_raw_choose_sides(buf, 40, 30, blk, 32, 24, &x0, &y0);
                assert(x0 == 8 && y0 == 6);
            }
        }

        /* Through the pipeline: raw opens a stop up, so at --ev -1 the
         * gray card renders as the same scene would from an 8-bit file;
         * the white patch is pulled under 1.0 by the scene shoulder; at
         * --ev 0 everything is brighter; and the headroom feeds halation:
         * the dark surround of the white patch glows more from raw than
         * from the display-referred copy. */
        PjImage *eight = pj_image_new(64, 48, &error);
        assert(eight);
        {
            float *q = pj_image_pixels(eight);
            memcpy(q, pj_image_pixels_const(decoded[4]), 64 * 48 * 3 * sizeof(float));
        }
        PjRenderOptions ro = {.seed = 3, .strength = 1.0f, .color_lut = identity,
                              .film_process = "negative"};
        PjRenderOptions rm1 = ro; rm1.ev = -1.0f;
        PjImage *from_raw_m1 = pj_render(decoded[0], "35mm-negative", &rm1, &error);
        PjImage *from_raw_0 = pj_render(decoded[0], "35mm-negative", &ro, &error);
        PjImage *from_8bit = pj_render(eight, "35mm-negative", &ro, &error);
        assert(from_raw_m1 && from_raw_0 && from_8bit);
        {
            float g_raw = luma_at(from_raw_m1, 30, 24), g_8 = luma_at(from_8bit, 30, 24);
            assert(fabs(g_raw - g_8) < 0.01);
            float w_raw = luma_at(from_raw_m1, 57, 8), w_8 = luma_at(from_8bit, 57, 8);
            assert(w_raw < w_8 - 0.02);
            assert(luma_at(from_raw_0, 30, 24) > g_raw * 1.5);
            /* halation: dark pixel two to the left of the white patch */
            float h_raw = luma_at(from_raw_0, 52, 8), h_8 = luma_at(from_8bit, 52, 8);
            assert(h_raw > h_8);
        }
        /* --halation: off removes the glow, 2 adds to it, 0 is the preset */
        PjRenderOptions hoff = ro; hoff.halation = PJ_HALATION_OFF;
        PjRenderOptions h2 = ro; h2.halation = 2.0f;
        PjRenderOptions h1 = ro; h1.halation = 1.0f;
        PjImage *r_off = pj_render(decoded[0], "35mm-negative", &hoff, &error);
        PjImage *r_2 = pj_render(decoded[0], "35mm-negative", &h2, &error);
        PjImage *r_1 = pj_render(decoded[0], "35mm-negative", &h1, &error);
        assert(r_off && r_2 && r_1);
        assert(luma_at(r_off, 52, 8) < luma_at(from_raw_0, 52, 8));
        assert(luma_at(r_2, 52, 8) > luma_at(from_raw_0, 52, 8));
        assert(memcmp(pj_image_pixels_const(r_1), pj_image_pixels_const(from_raw_0),
                      pj_image_width(r_1) * pj_image_height(r_1) * 3 * sizeof(float)) == 0);
        pj_image_free(r_off); pj_image_free(r_2); pj_image_free(r_1);
        pj_image_free(from_raw_m1); pj_image_free(from_raw_0); pj_image_free(from_8bit);
        pj_image_free(eight);
        for (int f = 0; f < 5; ++f) pj_image_free(decoded[f]);
        /* the door refuses what it does not understand */
        assert(!pj_image_load("tests/test_core.c", &error));
    }

    /* 1.18: the sRGB transfer runs through interpolated tables. They
     * must sit within 2e-5 of the exact curve everywhere, decode 8-bit
     * values exactly, and reproduce the 1.17.1 renders in tests/ref/
     * (made with powf) to within one 8-bit level on a sliver of
     * pixels: the references pin both the tables and the threaded
     * stages, which are required to be bit-identical to the serial
     * loops. */
    {
        float worst_e = 0.0f, worst_d = 0.0f;
        for (int i = 0; i <= 200000; ++i) {
            float x = (float)i / 200000.0f;
            float e_exact = x <= 0.0031308f ? 12.92f * x
                          : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
            float d_exact = x <= 0.04045f ? x / 12.92f
                          : powf((x + 0.055f) / 1.055f, 2.4f);
            worst_e = fmaxf(worst_e, fabsf(pj_srgb_encode(x) - e_exact));
            worst_d = fmaxf(worst_d, fabsf(pj_srgb_decode(x) - d_exact));
        }
        assert(worst_e < 2e-5f);
        assert(worst_d < 2e-5f);
        for (int v = 0; v < 256; ++v) {
            float back = 255.0f * pj_srgb_encode(pj_srgb_decode8((unsigned char)v));
            assert((int)lrintf(back) == v);
        }
        const char *cams[5] = {"35mm-negative", "bw-35", "super8",
                               "polaroid-600", "disposable-flash"};
        PjImage *tiny = pj_image_load("tests/raw/linear16.png", &error);
        assert(tiny);
        for (int k = 0; k < 5; ++k) {
            char refpath[128];
            snprintf(refpath, sizeof refpath, "tests/ref/%s.png", cams[k]);
            PjImage *ref = pj_image_load(refpath, &error);
            assert(ref);
            PjRenderOptions ro = {.seed = 7, .strength = 1.0f,
                                  .grain_model = "silver", .edge = 1.0f};
            PjImage *out = pj_render(tiny, cams[k], &ro, &error);
            assert(out);
            assert(pj_image_width(out) == pj_image_width(ref));
            assert(pj_image_height(out) == pj_image_height(ref));
            const float *a = pj_image_pixels_const(ref), *b = pj_image_pixels_const(out);
            size_t n = pj_image_width(out) * pj_image_height(out) * 3, off = 0;
            int worst = 0;
            for (size_t i = 0; i < n; ++i) {
                int la = (int)lrintf(255.0f * pj_srgb_encode(a[i]));
                int lb = (int)lrintf(255.0f * pj_srgb_encode(b[i]));
                int d = abs(la - lb);
                if (d) ++off;
                if (d > worst) worst = d;
            }
            assert(worst <= 1);
            assert(off * 1000 <= n);            /* under 0.1% of samples */
            pj_image_free(out);
            pj_image_free(ref);
        }
        pj_image_free(tiny);
    }

    /* 1.21: development text round-trips, the extended push range, and
     * the format crop switch. */
    {
        struct { const char *text; float push; bool cross; bool ok; } cases[] = {
            {"normal", 0, false, true}, {"push+1", 1, false, true},
            {"push+2", 2, false, true}, {"pull-1", -1, false, true},
            {"cross", 0, true, true}, {"push+4", 4, false, true},
            {"pull-3", -3, false, true}, {"push+1.5", 1.5f, false, true},
            {"cross+2", 2, true, true}, {"cross-1", -1, true, true},
            {"pull1", -1, false, true}, {"push2", 2, false, true},
            {"push+5", 0, false, false}, {"pull-4", 0, false, false},
            {"push-1", 0, false, false}, {"push", 0, false, false},
            {"fast", 0, false, false}, {"", 0, false, false},
        };
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            float push = 99.0f; bool cross = true;
            bool ok = pj_develop_parse(cases[i].text, &push, &cross);
            assert(ok == cases[i].ok);
            if (!ok) continue;
            assert(fabsf(push - cases[i].push) < 1e-6f && cross == cases[i].cross);
            char text[48];
            pj_develop_format(push, cross, text, sizeof text);
            float push2; bool cross2;
            assert(pj_develop_parse(text, &push2, &cross2));
            assert(push2 == push && cross2 == cross);
        }
        assert(pj_crop_parse("camera") == PJ_CROP_CAMERA);
        assert(pj_crop_parse("") == PJ_CROP_CAMERA);
        assert(pj_crop_parse("full") == PJ_CROP_FULL);
        assert(pj_crop_parse("square") < 0);
        assert(!strcmp(pj_crop_name(PJ_CROP_FULL), "full"));

        /* contrast keeps rising past +2 and keeps falling below -1, but
         * each further stop buys less than the one before */
        PjImage *card = pj_image_new(48, 48, &error);
        assert(card);
        fill_test_pattern(card);
        float spread[7];
        const float stops[7] = {-3, -2, -1, 0, 2, 3, 4};
        for (int i = 0; i < 7; ++i) {
            PjRenderOptions o = {.seed = 1, .strength = 1.0f, .push = stops[i]};
            PjImage *out = pj_render(card, "35mm-negative", &o, &error);
            assert(out);
            /* two midtones that stay in range at every stop */
            spread[i] = luma_at(out, 22, 22) - luma_at(out, 10, 10);
            pj_image_free(out);
        }
        for (int i = 1; i < 7; ++i) assert(spread[i] > spread[i - 1]);
        /* a stop beyond +2 buys less than a stop within 0..+2; a stop
         * below -1 takes less than the stop from 0 to -1 */
        assert(spread[5] - spread[4] < (spread[4] - spread[3]) / 2.0f);
        assert(spread[6] - spread[5] < (spread[4] - spread[3]) / 2.0f);
        assert(spread[2] - spread[1] < spread[3] - spread[2]);
        assert(spread[1] - spread[0] < spread[3] - spread[2]);
        /* within the old range nothing moved: +2 is +2 */
        {
            PjRenderOptions o = {.seed = 1, .strength = 1.0f, .push = 2.0f};
            PjRenderOptions q = {.seed = 1, .strength = 1.0f, .push = 2.0f, .crop = PJ_CROP_CAMERA};
            PjImage *a = pj_render(card, "35mm-negative", &o, &error);
            PjImage *b = pj_render(card, "35mm-negative", &q, &error);
            assert(a && b);
            assert(!memcmp(pj_image_pixels(a), pj_image_pixels(b), 48 * 48 * 3 * sizeof(float)));
            pj_image_free(a); pj_image_free(b);
        }

        /* crop: the camera's format cuts, full keeps the input's frame;
         * the instant border then wraps the whole frame */
        PjImage *wide = pj_image_new(64, 40, &error);
        assert(wide);
        fill_test_pattern(wide);
        assert(pj_preset_format("super8") != NULL);
        assert(pj_preset_format("35mm-negative") == NULL);
        assert(pj_preset_format("polaroid-600") != NULL);
        {
            PjRenderOptions cam = {.seed = 1, .strength = 1.0f};
            PjRenderOptions full = {.seed = 1, .strength = 1.0f, .crop = PJ_CROP_FULL};
            PjImage *a = pj_render(wide, "super8", &cam, &error);
            PjImage *b = pj_render(wide, "super8", &full, &error);
            assert(a && b);
            assert(pj_image_width(a) < 64 && pj_image_height(a) == 40);   /* 1.36:1 from 1.6:1 */
            assert(pj_image_width(b) == 64 && pj_image_height(b) == 40);
            pj_image_free(a); pj_image_free(b);
            a = pj_render(wide, "polaroid-600", &cam, &error);
            b = pj_render(wide, "polaroid-600", &full, &error);
            assert(a && b);
            /* the camera's print is near square plus its border; the full
             * frame keeps 64:40 inside the border, so it comes out wider */
            assert(pj_image_width(b) > pj_image_width(a));
            assert((double)pj_image_width(b) / pj_image_height(b) >
                   (double)pj_image_width(a) / pj_image_height(a));
            pj_image_free(a); pj_image_free(b);
            /* a camera without a format crop is the same either way */
            a = pj_render(wide, "35mm-negative", &cam, &error);
            b = pj_render(wide, "35mm-negative", &full, &error);
            assert(a && b);
            assert(!memcmp(pj_image_pixels(a), pj_image_pixels(b), 64 * 40 * 3 * sizeof(float)));
            pj_image_free(a); pj_image_free(b);
        }
        /* the geometry the engine reports is the one it renders */
        {
            const char *cams[] = {"polaroid-600", "super8", "toy-camera-120", "35mm-negative", "technicolor-3strip", "polaroid-packfilm"};
            for (size_t c = 0; c < sizeof cams / sizeof cams[0]; ++c)
                for (int crop = PJ_CROP_CAMERA; crop <= PJ_CROP_FULL; ++crop) {
                    PjGeometry g;
                    assert(pj_render_geometry(cams[c], 64, 40, crop, &g));
                    PjRenderOptions o = {.seed = 1, .strength = 1.0f, .crop = crop};
                    PjImage *out = pj_render(wide, cams[c], &o, &error);
                    assert(out);
                    assert(pj_image_width(out) == g.out_w && pj_image_height(out) == g.out_h);
                    assert(g.crop_x + g.crop_w <= 64 && g.crop_y + g.crop_h <= 40);
                    assert(g.place_x + g.crop_w <= g.out_w && g.place_y + g.crop_h <= g.out_h);
                    if (crop == PJ_CROP_FULL) assert(g.crop_w == 64 && g.crop_h == 40);
                    pj_image_free(out);
                }
            assert(!pj_render_geometry("no-such-camera", 64, 40, 0, &(PjGeometry){0}));
            char line[160];
            PjGeometry g;
            pj_render_geometry("polaroid-600", 64, 40, PJ_CROP_FULL, &g);
            pj_geometry_format(&g, line, sizeof line);
            assert(!strncmp(line, "# geometry in=64,40 crop=0,0,64,40 place=", 41));
        }
        /* the recipe carries the crop and the new develop text */
        {
            PjRenderOptions o = {.seed = 3, .strength = 1.0f, .push = 3.0f, .crop = PJ_CROP_FULL};
            char text[1024];
            pj_recipe_format("super8", "none", NULL, &o, text, sizeof text);
            assert(strstr(text, "\ndevelop=push+3\n"));
            assert(strstr(text, "\ncrop=full\n"));
            o.push = -2.5f; o.cross_process = true; o.crop = PJ_CROP_CAMERA;
            pj_recipe_format("super8", "none", NULL, &o, text, sizeof text);
            assert(strstr(text, "\ndevelop=cross-2.5\n"));
            assert(strstr(text, "\ncrop=camera\n"));
        }
        pj_image_free(wide);
        pj_image_free(card);
    }

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
