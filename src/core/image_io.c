/*
 * Format-dispatching image I/O.
 *
 * pj_image_load()/pj_image_save() choose a codec from the file extension:
 * .ppm uses the built-in dependency-free reader/writer, .png and
 * .jpg/.jpeg use the vendored stb libraries.  All formats are treated as
 * 8-bit sRGB at the boundary and converted to linear RGB for processing,
 * exactly as the PPM path always did.
 *
 * stb_image deliberately ignores EXIF metadata, but phone JPEGs are very
 * often stored rotated with only an orientation tag to say so.  We
 * therefore parse the JPEG APP1/TIFF structure for tag 0x0112 ourselves
 * and bake the indicated transform into the pixels, which is what
 * "--auto-orient" does in ImageMagick.
 */
#include "internal.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"
#include "stb_image_write.h"

static float clamp01f(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

/* 8-bit decode is exact (256 entries); encode goes through the shared
 * interpolated table, see image.c */
static float io_linear_to_srgb(float x)
{
    return pj_srgb_encode(clamp01f(x));
}

typedef enum { PJ_FMT_UNKNOWN, PJ_FMT_PPM, PJ_FMT_PNG, PJ_FMT_JPEG, PJ_FMT_RAW } PjFormat;

static PjFormat format_from_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return PJ_FMT_UNKNOWN;
    char ext[8] = {0};
    size_t n = strlen(dot + 1);
    if (n == 0 || n >= sizeof ext) return PJ_FMT_UNKNOWN;
    for (size_t i = 0; i < n; ++i) ext[i] = (char)tolower((unsigned char)dot[1 + i]);
    if (!strcmp(ext, "ppm")) return PJ_FMT_PPM;
    if (!strcmp(ext, "png")) return PJ_FMT_PNG;
    if (!strcmp(ext, "jpg") || !strcmp(ext, "jpeg")) return PJ_FMT_JPEG;
    if (!strcmp(ext, "nef") || !strcmp(ext, "dng")) return PJ_FMT_RAW;
    return PJ_FMT_UNKNOWN;
}

/* ---- EXIF orientation ------------------------------------------------- */

static unsigned read_u16(const unsigned char *p, bool big_endian)
{
    return big_endian ? (unsigned)(p[0] << 8 | p[1])
                      : (unsigned)(p[1] << 8 | p[0]);
}

static unsigned long read_u32(const unsigned char *p, bool big_endian)
{
    return big_endian
        ? ((unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
           (unsigned long)p[2] << 8 | p[3])
        : ((unsigned long)p[3] << 24 | (unsigned long)p[2] << 16 |
           (unsigned long)p[1] << 8 | p[0]);
}

/* Returns the EXIF orientation (1..8), or 1 when absent or unparseable. */
static int jpeg_exif_orientation(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 1;

    int orientation = 1;
    unsigned char head[2];
    if (fread(head, 1, 2, file) != 2 || head[0] != 0xFF || head[1] != 0xD8)
        goto done;

    /* Walk JPEG segments looking for APP1/Exif. */
    for (;;) {
        unsigned char marker[2], lenbuf[2];
        if (fread(marker, 1, 2, file) != 2 || marker[0] != 0xFF) goto done;
        if (marker[1] == 0xD9 || marker[1] == 0xDA) goto done;  /* EOI/SOS */
        if (fread(lenbuf, 1, 2, file) != 2) goto done;
        long seg_len = (long)(lenbuf[0] << 8 | lenbuf[1]) - 2;
        if (seg_len < 0) goto done;
        if (marker[1] != 0xE1) {                                 /* not APP1 */
            if (fseek(file, seg_len, SEEK_CUR) != 0) goto done;
            continue;
        }
        unsigned char *seg = malloc((size_t)seg_len);
        if (!seg || fread(seg, 1, (size_t)seg_len, file) != (size_t)seg_len) {
            free(seg);
            goto done;
        }
        if (seg_len >= 14 && memcmp(seg, "Exif\0\0", 6) == 0) {
            const unsigned char *tiff = seg + 6;
            long tiff_len = seg_len - 6;
            bool be;
            if (!memcmp(tiff, "MM", 2)) be = true;
            else if (!memcmp(tiff, "II", 2)) be = false;
            else { free(seg); goto done; }
            if (read_u16(tiff + 2, be) == 42) {
                unsigned long ifd = read_u32(tiff + 4, be);
                if (ifd + 2 <= (unsigned long)tiff_len) {
                    unsigned count = read_u16(tiff + ifd, be);
                    for (unsigned i = 0; i < count; ++i) {
                        unsigned long entry = ifd + 2 + 12ul * i;
                        if (entry + 12 > (unsigned long)tiff_len) break;
                        if (read_u16(tiff + entry, be) == 0x0112) {
                            unsigned value = read_u16(tiff + entry + 8, be);
                            if (value >= 1 && value <= 8)
                                orientation = (int)value;
                            break;
                        }
                    }
                }
            }
        }
        free(seg);
        goto done;   /* orientation lives in the first APP1; stop either way */
    }

done:
    fclose(file);
    return orientation;
}

typedef struct { const PjImage *in; PjImage *out; int orientation; } OrientCtx;

static void orient_band(size_t y0, size_t y1, void *vctx)
{
    OrientCtx *o = vctx;
    const PjImage *image = o->in;
    PjImage *out = o->out;
    size_t out_w = out->width;
    int orientation = o->orientation;
    for (size_t y = y0; y < y1; ++y) {
        for (size_t x = 0; x < image->width; ++x) {
            size_t ox = 0, oy = 0;
            switch (orientation) {
            case 2: ox = image->width - 1 - x;  oy = y;                       break;
            case 3: ox = image->width - 1 - x;  oy = image->height - 1 - y;   break;
            case 4: ox = x;                     oy = image->height - 1 - y;   break;
            case 5: ox = y;                     oy = x;                       break;
            case 6: ox = image->height - 1 - y; oy = x;                       break;
            case 7: ox = image->height - 1 - y; oy = image->width - 1 - x;    break;
            case 8: ox = y;                     oy = image->width - 1 - x;    break;
            }
            memcpy(&out->rgb[(oy * out_w + ox) * 3],
                   &image->rgb[(y * image->width + x) * 3],
                   3 * sizeof(float));
        }
    }
}

/* Bake EXIF orientation 2..8 into the pixel buffer. */
PjImage *pj_image_apply_orientation(PjImage *image, int orientation,
                                  PjError *error)
{
    if (orientation <= 1 || orientation > 8 || !image) return image;

    bool swaps = orientation >= 5;      /* 5..8 transpose the axes */
    size_t out_w = swaps ? image->height : image->width;
    size_t out_h = swaps ? image->width : image->height;
    PjImage *out = pj_image_new(out_w, out_h, error);
    if (!out) {
        pj_image_free(image);
        return NULL;
    }
    out->scene_linear = image->scene_linear;
    OrientCtx o = {image, out, orientation};
    pj_parallel_rows(image->height, orient_band, &o);
    pj_image_free(image);
    return out;
}

/* The first COM segment of a JPEG that holds a polajuice recipe. */
char *pj_jpeg_read_comment(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char head[2];
    if (fread(head, 1, 2, f) != 2 || head[0] != 0xFF || head[1] != 0xD8) { fclose(f); return NULL; }
    char *result = NULL;
    for (;;) {
        int c = fgetc(f);
        if (c != 0xFF) break;
        int marker = fgetc(f);
        while (marker == 0xFF) marker = fgetc(f);
        if (marker == EOF || marker == 0xDA || marker == 0xD9) break;   /* scan data: stop */
        int l1 = fgetc(f), l2 = fgetc(f);
        if (l1 == EOF || l2 == EOF) break;
        long len = (l1 << 8 | l2) - 2;
        if (len < 0) break;
        if (marker == 0xFE && len > 0 && len < 65536) {
            char *text = malloc((size_t)len + 1);
            if (!text) break;
            if (fread(text, 1, (size_t)len, f) != (size_t)len) { free(text); break; }
            text[len] = '\0';
            if (pj_recipe_is_recipe(text)) { result = text; break; }
            free(text);
            continue;
        }
        if (fseek(f, len, SEEK_CUR) != 0) break;
    }
    fclose(f);
    return result;
}

/* ---- load / save ------------------------------------------------------ */

static PjImage *load_stb(const char *path, PjError *error)
{
    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 3);
    if (!pixels) {
        pj_set_error(error, "cannot decode '%s': %s", path,
                     stbi_failure_reason());
        return NULL;
    }
    PjImage *image = pj_image_new((size_t)width, (size_t)height, error);
    if (!image) {
        stbi_image_free(pixels);
        return NULL;
    }
    size_t samples = (size_t)width * (size_t)height * 3;
    pj_srgb_tables_init();
    for (size_t i = 0; i < samples; ++i)
        image->rgb[i] = pj_srgb_decode8(pixels[i]);
    stbi_image_free(pixels);
    return image;
}

typedef struct { const PjImage *image; unsigned char *out; } EncodeCtx;

static void encode_band(size_t y0, size_t y1, void *vctx)
{
    EncodeCtx *e = vctx;
    size_t w = e->image->width;
    for (size_t i = y0 * w * 3; i < y1 * w * 3; ++i)
        e->out[i] = (unsigned char)lrintf(255.0f * io_linear_to_srgb(e->image->rgb[i]));
}

PjImage *pj_image_load(const char *path, PjError *error)
{
    if (!path) {
        pj_set_error(error, "missing input path");
        return NULL;
    }
    switch (format_from_path(path)) {
    case PJ_FMT_PPM:
        return pj_image_load_ppm(path, error);
    case PJ_FMT_PNG:
        return load_stb(path, error);
    case PJ_FMT_JPEG: {
        PjImage *image = load_stb(path, error);
        if (!image) return NULL;
        return pj_image_apply_orientation(image, jpeg_exif_orientation(path), error);
    }
    case PJ_FMT_RAW:
        return pj_image_load_raw(path, error);
    default:
        pj_set_error(error,
                     "'%s': unsupported format (use .ppm, .png, .jpg, .jpeg, "
                     ".nef or .dng)",
                     path);
        return NULL;
    }
}

bool pj_image_save(const PjImage *image, const char *path, PjError *error)
{
    if (!image || !path) {
        pj_set_error(error, "missing image or output path");
        return false;
    }
    PjFormat format = format_from_path(path);
    if (format == PJ_FMT_PPM)
        return pj_image_save_ppm(image, path, error);
    if (format == PJ_FMT_UNKNOWN) {
        pj_set_error(error,
                     "'%s': unsupported format (use .ppm, .png, .jpg or .jpeg)",
                     path);
        return false;
    }
    if (image->width > INT_MAX || image->height > INT_MAX) {
        pj_set_error(error, "image too large for %s encoder", "stb");
        return false;
    }

    size_t samples = image->width * image->height * 3;
    pj_srgb_tables_init();
    unsigned char *encoded = malloc(samples);
    if (!encoded) {
        pj_set_error(error, "could not allocate output buffer");
        return false;
    }
    EncodeCtx ec = {image, encoded};
    pj_parallel_rows(image->height, encode_band, &ec);

    int ok = 0;
    if (format == PJ_FMT_PNG)
        ok = stbi_write_png(path, (int)image->width, (int)image->height, 3,
                            encoded, (int)(image->width * 3));
    else {
        bool wrote = pj_jpeg_write(path, encoded, image->width, image->height,
                                   92, image->comment, error);
        free(encoded);
        return wrote;
    }
    free(encoded);
    if (!ok) pj_set_error(error, "failed while writing '%s'", path);
    return ok != 0;
}
