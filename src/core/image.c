#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static float srgb_to_linear(float x)
{
    return x <= 0.04045f ? x / 12.92f
                         : powf((x + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float x)
{
    x = clamp01(x);
    return x <= 0.0031308f ? 12.92f * x
                           : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

void pj_set_error(PjError *error, const char *format, ...)
{
    if (!error) return;
    va_list args;
    va_start(args, format);
    (void)vsnprintf(error->message, sizeof error->message, format, args);
    va_end(args);
}

PjImage *pj_image_new(size_t width, size_t height, PjError *error)
{
    if (width == 0 || height == 0 || width > SIZE_MAX / height ||
        width * height > SIZE_MAX / (3 * sizeof(float))) {
        pj_set_error(error, "invalid or excessive image dimensions");
        return NULL;
    }

    PjImage *image = calloc(1, sizeof *image);
    if (!image) {
        pj_set_error(error, "could not allocate image descriptor");
        return NULL;
    }
    image->rgb = calloc(width * height * 3, sizeof *image->rgb);
    if (!image->rgb) {
        free(image);
        pj_set_error(error, "could not allocate %zux%zu image", width, height);
        return NULL;
    }
    image->width = width;
    image->height = height;
    return image;
}

void pj_image_free(PjImage *image)
{
    if (!image) return;
    free(image->rgb);
    free(image);
}

size_t pj_image_width(const PjImage *image) { return image ? image->width : 0; }
size_t pj_image_height(const PjImage *image) { return image ? image->height : 0; }
float *pj_image_pixels(PjImage *image) { return image ? image->rgb : NULL; }
const float *pj_image_pixels_const(const PjImage *image)
{
    return image ? image->rgb : NULL;
}

static bool read_token(FILE *file, char *token, size_t capacity)
{
    int c;
    do {
        c = fgetc(file);
        if (c == '#') {
            do c = fgetc(file); while (c != '\n' && c != EOF);
        }
    } while (isspace(c));

    if (c == EOF) return false;
    size_t n = 0;
    do {
        if (n + 1 < capacity) token[n++] = (char)c;
        c = fgetc(file);
    } while (c != EOF && !isspace(c) && c != '#');
    if (c == '#') {
        do c = fgetc(file); while (c != '\n' && c != EOF);
    }
    token[n] = '\0';
    return n != 0;
}

static bool parse_size(const char *text, size_t *value)
{
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || !end || *end || parsed == 0 || parsed > SIZE_MAX) return false;
    *value = (size_t)parsed;
    return true;
}

PjImage *pj_image_load_ppm(const char *path, PjError *error)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        pj_set_error(error, "cannot open '%s': %s", path, strerror(errno));
        return NULL;
    }

    char token[64];
    size_t width = 0, height = 0, maximum = 0;
    if (!read_token(file, token, sizeof token) || strcmp(token, "P6") != 0 ||
        !read_token(file, token, sizeof token) || !parse_size(token, &width) ||
        !read_token(file, token, sizeof token) || !parse_size(token, &height) ||
        !read_token(file, token, sizeof token) || !parse_size(token, &maximum) ||
        maximum != 255) {
        fclose(file);
        pj_set_error(error, "'%s' is not an 8-bit binary PPM (P6)", path);
        return NULL;
    }

    PjImage *image = pj_image_new(width, height, error);
    if (!image) {
        fclose(file);
        return NULL;
    }

    size_t bytes = width * height * 3;
    unsigned char *encoded = malloc(bytes);
    if (!encoded || fread(encoded, 1, bytes, file) != bytes) {
        free(encoded);
        pj_image_free(image);
        fclose(file);
        pj_set_error(error, "truncated or unreadable pixel data in '%s'", path);
        return NULL;
    }
    fclose(file);

    for (size_t i = 0; i < bytes; ++i)
        image->rgb[i] = srgb_to_linear((float)encoded[i] / 255.0f);
    free(encoded);
    return image;
}

bool pj_image_save_ppm(const PjImage *image, const char *path, PjError *error)
{
    if (!image || !path) {
        pj_set_error(error, "missing image or output path");
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        pj_set_error(error, "cannot create '%s': %s", path, strerror(errno));
        return false;
    }
    if (fprintf(file, "P6\n%zu %zu\n255\n", image->width, image->height) < 0) {
        fclose(file);
        pj_set_error(error, "cannot write header to '%s'", path);
        return false;
    }

    size_t samples = image->width * image->height * 3;
    unsigned char *encoded = malloc(samples);
    if (!encoded) {
        fclose(file);
        pj_set_error(error, "could not allocate output buffer");
        return false;
    }
    for (size_t i = 0; i < samples; ++i)
        encoded[i] = (unsigned char)lrintf(255.0f * linear_to_srgb(image->rgb[i]));

    bool ok = fwrite(encoded, 1, samples, file) == samples && fclose(file) == 0;
    free(encoded);
    if (!ok) pj_set_error(error, "failed while writing '%s'", path);
    return ok;
}

const char *pj_version_string(void) { return "1.10.0"; }

PjImage *pj_image_downscale(PjImage *image, size_t max_dim, PjError *error)
{
    if (max_dim == 0 || !image) return image;
    size_t w = pj_image_width(image), h = pj_image_height(image);
    size_t longest = w > h ? w : h;
    if (longest <= max_dim) return image;

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
