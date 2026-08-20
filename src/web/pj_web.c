/*
 * WebAssembly entry points for polajuice.
 *
 * The shim deliberately works on *paths*: the browser writes the uploaded
 * image to /in.img and the chosen film to /film.cube in Emscripten's
 * in-memory filesystem (MEMFS), then calls pj_web_render().  Reusing the
 * path-based core API means the exact same code runs as on the desktop —
 * including stb decoding and the EXIF orientation parser, which reads the
 * file itself — so a given input, camera, film, strength and seed produces
 * byte-identical pixels in the browser and the CLI.
 *
 * This file is plain C17: EMSCRIPTEN_KEEPALIVE degrades to nothing under a
 * native compiler, so the shim can be built and tested natively too (see
 * tests/test_web_shim.c).
 */
#include "polajuice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

/* Fixed exchange paths. Under Emscripten these live in MEMFS, whose root
 * is writable. Natively (the shim is also compiled for tests) the root
 * filesystem is not writable for ordinary users, so use /tmp there --
 * a distinction the sandbox this was first tested in masked by running
 * as root. */
#ifdef __EMSCRIPTEN__
#define PJ_WEB_FILM   "/film.cube"
#define PJ_WEB_OUTPUT "/out.jpg"
#else
/* Per-uid names: fixed /tmp paths collide between users on shared hosts. */
#include <unistd.h>
static const char *native_path(const char *stem, const char *suffix)
{
    static char film[64], out[64];
    char *buffer = stem[0] == 'f' ? film : out;
    snprintf(buffer, 64, "/tmp/pj_web_%s_%ld%s", stem, (long)getuid(), suffix);
    return buffer;
}
#define PJ_WEB_FILM   native_path("film", ".cube")
#define PJ_WEB_OUTPUT native_path("out", ".jpg")
#endif

static char status_message[512];

static void set_status(const char *text)
{
    snprintf(status_message, sizeof status_message, "%s", text);
}

EMSCRIPTEN_KEEPALIVE const char *pj_web_status(void)
{
    return status_message;
}

EMSCRIPTEN_KEEPALIVE const char *pj_web_version(void)
{
    return pj_version_string();
}

/* ---- camera enumeration for the UI ------------------------------------ */

EMSCRIPTEN_KEEPALIVE int pj_web_camera_count(void)
{
    return (int)pj_preset_count();
}

EMSCRIPTEN_KEEPALIVE const char *pj_web_camera_name(int index)
{
    if (index < 0 || (size_t)index >= pj_preset_count()) return "";
    return pj_preset_name((size_t)index);
}

EMSCRIPTEN_KEEPALIVE const char *pj_web_camera_description(int index)
{
    const char *name = pj_web_camera_name(index);
    const char *text = pj_preset_description(name);
    return text ? text : "";
}

/* Canonical film stem for a camera, or "" when scalars carry the look. */
EMSCRIPTEN_KEEPALIVE const char *pj_web_camera_film(int index)
{
    const char *name = pj_web_camera_name(index);
    const char *film = pj_preset_default_film(name);
    return film ? film : "";
}

/* ---- rendering --------------------------------------------------------- */

/* Box-average downscale for previews; full-quality renders pass max_dim 0.
 * Averaging in linear light is the correct place for it, and the pipeline
 * is resolution-aware (blur radii and halation scale with the diagonal),
 * so a preview is a faithful miniature rather than a different picture. */
static PjImage *downscale(PjImage *image, int max_dim, PjError *error)
{
    if (max_dim <= 0) return image;
    size_t w = pj_image_width(image), h = pj_image_height(image);
    size_t longest = w > h ? w : h;
    if (longest <= (size_t)max_dim) return image;

    double scale = (double)max_dim / (double)longest;
    size_t out_w = (size_t)(w * scale + 0.5);
    size_t out_h = (size_t)(h * scale + 0.5);
    if (out_w < 1) out_w = 1;
    if (out_h < 1) out_h = 1;

    PjImage *out = pj_image_new(out_w, out_h, error);
    if (!out) {
        pj_image_free(image);
        return NULL;
    }
    const float *src = pj_image_pixels_const(image);
    float *dst = pj_image_pixels(out);
    for (size_t y = 0; y < out_h; ++y) {
        size_t y0 = y * h / out_h, y1 = (y + 1) * h / out_h;
        if (y1 <= y0) y1 = y0 + 1;
        for (size_t x = 0; x < out_w; ++x) {
            size_t x0 = x * w / out_w, x1 = (x + 1) * w / out_w;
            if (x1 <= x0) x1 = x0 + 1;
            double sum[3] = {0, 0, 0};
            for (size_t sy = y0; sy < y1; ++sy)
                for (size_t sx = x0; sx < x1; ++sx)
                    for (size_t c = 0; c < 3; ++c)
                        sum[c] += src[(sy * w + sx) * 3 + c];
            double count = (double)((y1 - y0) * (x1 - x0));
            for (size_t c = 0; c < 3; ++c)
                dst[(y * out_w + x) * 3 + c] = (float)(sum[c] / count);
        }
    }
    pj_image_free(image);
    return out;
}

/*
 * Render input_path (browser writes /in.jpg or /in.png so that format
 * dispatch and JPEG EXIF handling work exactly as on the desktop) with the
 * given camera.  use_film: 0 = scalar engine, 1 = load /film.cube.
 * max_dim > 0 downscales first (preview mode).  Writes /out.jpg; returns
 * its byte size, or 0 on failure (message via pj_web_status()).
 */
EMSCRIPTEN_KEEPALIVE int pj_web_render(const char *input_path,
                                       const char *camera, int use_film,
                                       float strength, double seed,
                                       int max_dim)
{
    PjError error = {{0}};
    set_status("ok");

    PjLut3D *lut = NULL;
    if (use_film) {
        lut = pj_lut3d_load_cube(PJ_WEB_FILM, &error);
        if (!lut) {
            set_status(error.message);
            return 0;
        }
    }

    PjImage *source = pj_image_load(input_path, &error);
    if (!source) {
        pj_lut3d_free(lut);
        set_status(error.message);
        return 0;
    }
    source = downscale(source, max_dim, &error);
    if (!source) {
        pj_lut3d_free(lut);
        set_status(error.message);
        return 0;
    }

    PjRenderOptions options = {.seed = (uint64_t)seed,
                               .strength = strength,
                               .color_lut = lut};
    PjImage *result = pj_render(source, camera, &options, &error);
    pj_image_free(source);
    pj_lut3d_free(lut);
    if (!result) {
        set_status(error.message);
        return 0;
    }

    bool saved = pj_image_save(result, PJ_WEB_OUTPUT, &error);
    pj_image_free(result);
    if (!saved) {
        set_status(error.message);
        return 0;
    }

    FILE *file = fopen(PJ_WEB_OUTPUT, "rb");
    if (!file) {
        set_status("cannot reopen rendered output");
        return 0;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size > 0 ? (int)size : 0;
}
