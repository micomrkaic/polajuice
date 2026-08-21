/*
 * superjuice - the juice engine's motion front-end.
 *
 * Reads YUV4MPEG2 (y4m) on stdin, renders every frame through the same
 * libpolajuice core the stills front-end uses - with the temporal axis
 * enabled: gate weave, exposure flicker, and frame-decorrelated grain for
 * cameras that define them - and writes y4m on stdout. Codecs are
 * FFmpeg's job, at both ends of a pipe:
 *
 *   ffmpeg -v error -i in.mp4 -f yuv4mpegpipe -pix_fmt yuv420p - \
 *     | superjuice -c super8 --seed 7 \
 *     | ffmpeg -v error -f yuv4mpegpipe -i - -i in.mp4 \
 *              -map 0:v -map 1:a? -c:v libx264 -crf 18 -c:a copy out.mp4
 *
 * Supported input chroma: C420 (all subvariants) and C444, 8-bit,
 * BT.601 limited range - which is what FFmpeg's yuv4mpegpipe emits for
 * yuv420p. Output is C444 to avoid a second subsampling loss.
 */
#define _POSIX_C_SOURCE 200809L

#include "polajuice.h"
#include "filmlib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clampf01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static float srgb_to_lin(float x)
{
    return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f);
}

static float lin_to_srgb(float x)
{
    x = clampf01(x);
    return x <= 0.0031308f ? 12.92f * x
                           : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

/* BT.601 limited-range YCbCr <-> gamma-encoded R'G'B' */
static void ycbcr_to_rgb(float y, float cb, float cr,
                         float *r, float *g, float *b)
{
    float yy = (y - 16.0f) / 219.0f;
    float pb = (cb - 128.0f) / 224.0f;
    float pr = (cr - 128.0f) / 224.0f;
    *r = clampf01(yy + 1.402f * pr);
    *g = clampf01(yy - 0.344136f * pb - 0.714136f * pr);
    *b = clampf01(yy + 1.772f * pb);
}

static void rgb_to_ycbcr(float r, float g, float b,
                         unsigned char *y, unsigned char *cb,
                         unsigned char *cr)
{
    float yy = 0.299f * r + 0.587f * g + 0.114f * b;
    float pb = (b - yy) / 1.772f;
    float pr = (r - yy) / 1.402f;
    long ly = lrintf(16.0f + 219.0f * yy);
    long lcb = lrintf(128.0f + 224.0f * pb);
    long lcr = lrintf(128.0f + 224.0f * pr);
    *y = (unsigned char)(ly < 0 ? 0 : ly > 255 ? 255 : ly);
    *cb = (unsigned char)(lcb < 0 ? 0 : lcb > 255 ? 255 : lcb);
    *cr = (unsigned char)(lcr < 0 ? 0 : lcr > 255 ? 255 : lcr);
}

static void usage(FILE *stream)
{
    fprintf(stream,
        "superjuice %s - motion front-end of the juice engine\n\n"
        "Usage: ffmpeg ... -f yuv4mpegpipe - | superjuice -c CAMERA "
        "[options] | ffmpeg -f yuv4mpegpipe -i - ...\n\n"
        "Options (as in polajuice):\n"
        "  -c, --camera NAME       camera archetype; instant cameras are\n"
        "                          still-print formats and are refused\n"
        "  -f, --film NAME|FILE    film stock (library name or .cube path)\n"
        "      --no-film / --any-film\n"
        "      --develop MODE      normal, push+1, push+2, pull-1, cross\n"
        "      --age NUMBER        0..1\n"
        "      --strength NUMBER   0..1\n"
        "      --seed INTEGER      base seed; grain decorrelates per frame\n\n"
        "Cameras with motion character (gate weave, flicker): super8,\n"
        "autochrome, technicolor-3strip; every camera gets frame-\n"
        "decorrelated 'boiling' grain.\n",
        pj_version_string());
}

typedef struct {
    size_t width, height;
    int chroma444;
    char params[256];   /* frame-rate/interlace/aspect tokens, passed through */
} Y4mHeader;

static bool read_y4m_header(FILE *in, Y4mHeader *header)
{
    char line[512];
    if (!fgets(line, sizeof line, in)) return false;
    if (strncmp(line, "YUV4MPEG2", 9)) return false;
    header->width = header->height = 0;
    header->chroma444 = 0;
    header->params[0] = '\0';
    char *cursor = line + 9;
    char *token;
    while ((token = strtok(cursor, " \n")) != NULL) {
        cursor = NULL;
        switch (token[0]) {
        case 'W': header->width = (size_t)strtoul(token + 1, NULL, 10); break;
        case 'H': header->height = (size_t)strtoul(token + 1, NULL, 10); break;
        case 'C':
            if (!strncmp(token, "C444", 4)) header->chroma444 = 1;
            else if (strncmp(token, "C420", 4)) {
                fprintf(stderr, "superjuice: unsupported chroma '%s' "
                        "(use -pix_fmt yuv420p)\n", token);
                return false;
            }
            break;
        default: {
            size_t used = strlen(header->params);
            snprintf(header->params + used, sizeof header->params - used,
                     " %s", token);
        }
        }
    }
    return header->width > 0 && header->height > 0;
}

int main(int argc, char **argv)
{
    const char *camera = NULL, *film_request = NULL;
    bool no_film = false, any_film = false;
    PjRenderOptions options = {.seed = UINT64_C(0x504f4c414a554943),
                               .strength = 1.0f, .temporal = true};

    for (int i = 1; i < argc; ++i) {
        if ((!strcmp(argv[i], "-c") || !strcmp(argv[i], "--camera")) && i + 1 < argc)
            camera = argv[++i];
        else if ((!strcmp(argv[i], "-f") || !strcmp(argv[i], "--film")) && i + 1 < argc)
            film_request = argv[++i];
        else if (!strcmp(argv[i], "--no-film")) no_film = true;
        else if (!strcmp(argv[i], "--any-film")) any_film = true;
        else if (!strcmp(argv[i], "--develop") && i + 1 < argc) {
            const char *dev = argv[++i];
            if (!strcmp(dev, "push+1")) options.push = 1.0f;
            else if (!strcmp(dev, "push+2")) options.push = 2.0f;
            else if (!strcmp(dev, "pull-1")) options.push = -1.0f;
            else if (!strcmp(dev, "cross")) options.cross_process = true;
            else if (strcmp(dev, "normal")) {
                fprintf(stderr, "invalid develop mode '%s'\n", dev);
                return EXIT_FAILURE;
            }
        }
        else if (!strcmp(argv[i], "--age") && i + 1 < argc)
            options.age = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--strength") && i + 1 < argc)
            options.strength = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            options.seed = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    if (!camera) { usage(stderr); return EXIT_FAILURE; }
    if (!pj_preset_description(camera)) {
        fprintf(stderr, "unknown camera: %s\n", camera);
        return EXIT_FAILURE;
    }
    if (pj_preset_is_instant(camera)) {
        fprintf(stderr, "superjuice: %s is a still-print format; instant "
                "cameras do not shoot movies\n", camera);
        return EXIT_FAILURE;
    }

    char *film_path = NULL;
    if (film_request) {
        char processes[256];
        if (pj_preset_film_processes(camera, processes, sizeof processes) &&
            processes[0] == '\0' && !any_film) {
            fprintf(stderr, "superjuice: %s is a sealed process and takes "
                    "no film (--any-film overrides)\n", camera);
            return EXIT_FAILURE;
        }
        film_path = resolve_film(film_request, false);
        if (!film_path) return EXIT_FAILURE;
        const char *process = film_process_of(film_path);
        if (process && !any_film && !pj_preset_accepts_film(camera, process)) {
            fprintf(stderr, "superjuice: %s takes %s; '%s' is %s film "
                    "(--any-film overrides)\n", camera,
                    processes[0] ? processes : "no film",
                    film_request, process);
            free(film_path);
            return EXIT_FAILURE;
        }
    } else if (!no_film) {
        const char *canonical = pj_preset_default_film(camera);
        if (canonical) {
            film_path = resolve_film(canonical, true);
            if (!film_path)
                fprintf(stderr, "note: canonical film '%s' not installed; "
                        "using the scalar engine\n", canonical);
        }
    }

    PjError error = {{0}};
    PjLut3D *lut = NULL;
    if (film_path) {
        lut = pj_lut3d_load_cube(film_path, &error);
        free(film_path);
        if (!lut) { fprintf(stderr, "superjuice: %s\n", error.message); return EXIT_FAILURE; }
        options.color_lut = lut;
    }

    Y4mHeader header;
    if (!read_y4m_header(stdin, &header)) {
        fprintf(stderr, "superjuice: stdin is not a YUV4MPEG2 stream "
                "(pipe from: ffmpeg -i IN -f yuv4mpegpipe -pix_fmt yuv420p -)\n");
        pj_lut3d_free(lut);
        return EXIT_FAILURE;
    }
    size_t w = header.width, h = header.height;
    size_t chroma_w = header.chroma444 ? w : (w + 1) / 2;
    size_t chroma_h = header.chroma444 ? h : (h + 1) / 2;
    unsigned char *y_plane = malloc(w * h);
    unsigned char *cb_plane = malloc(chroma_w * chroma_h);
    unsigned char *cr_plane = malloc(chroma_w * chroma_h);
    if (!y_plane || !cb_plane || !cr_plane) {
        fprintf(stderr, "superjuice: out of memory\n");
        return EXIT_FAILURE;
    }

    int64_t frame = 0;
    size_t out_w = 0, out_h = 0;
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        if (strncmp(line, "FRAME", 5)) break;
        if (fread(y_plane, 1, w * h, stdin) != w * h ||
            fread(cb_plane, 1, chroma_w * chroma_h, stdin) != chroma_w * chroma_h ||
            fread(cr_plane, 1, chroma_w * chroma_h, stdin) != chroma_w * chroma_h) {
            fprintf(stderr, "superjuice: truncated frame %lld\n",
                    (long long)frame);
            break;
        }

        PjImage *image = pj_image_new(w, h, &error);
        if (!image) { fprintf(stderr, "superjuice: %s\n", error.message); break; }
        float *px = pj_image_pixels(image);
        for (size_t yy = 0; yy < h; ++yy)
            for (size_t xx = 0; xx < w; ++xx) {
                size_t ci = header.chroma444
                    ? yy * chroma_w + xx
                    : (yy / 2) * chroma_w + xx / 2;
                float r, g, b;
                ycbcr_to_rgb((float)y_plane[yy * w + xx],
                             (float)cb_plane[ci], (float)cr_plane[ci],
                             &r, &g, &b);
                size_t o = (yy * w + xx) * 3;
                px[o] = srgb_to_lin(r);
                px[o + 1] = srgb_to_lin(g);
                px[o + 2] = srgb_to_lin(b);
            }

        options.frame = frame;
        PjImage *rendered = pj_render(image, camera, &options, &error);
        pj_image_free(image);
        if (!rendered) { fprintf(stderr, "superjuice: %s\n", error.message); break; }

        size_t rw = pj_image_width(rendered), rh = pj_image_height(rendered);
        if (frame == 0) {
            out_w = rw;
            out_h = rh;
            printf("YUV4MPEG2 W%zu H%zu C444%s\n", out_w, out_h,
                   header.params);
        } else if (rw != out_w || rh != out_h) {
            fprintf(stderr, "superjuice: frame size changed mid-stream\n");
            pj_image_free(rendered);
            break;
        }
        fputs("FRAME\n", stdout);
        const float *rp = pj_image_pixels_const(rendered);
        /* C444 out: three full planes */
        static unsigned char *out_planes[3] = {NULL, NULL, NULL};
        for (int pl = 0; pl < 3; ++pl)
            if (!out_planes[pl]) out_planes[pl] = malloc(out_w * out_h);
        for (size_t i = 0; i < out_w * out_h; ++i) {
            float r = lin_to_srgb(rp[i * 3]);
            float g = lin_to_srgb(rp[i * 3 + 1]);
            float b = lin_to_srgb(rp[i * 3 + 2]);
            rgb_to_ycbcr(r, g, b, &out_planes[0][i], &out_planes[1][i],
                         &out_planes[2][i]);
        }
        for (int pl = 0; pl < 3; ++pl)
            fwrite(out_planes[pl], 1, out_w * out_h, stdout);
        pj_image_free(rendered);

        ++frame;
        if (frame % 24 == 0)
            fprintf(stderr, "\rsuperjuice: %lld frames", (long long)frame);
    }
    if (frame >= 24) fputc('\n', stderr);
    fprintf(stderr, "superjuice: done, %lld frames\n", (long long)frame);
    free(y_plane); free(cb_plane); free(cr_plane);
    pj_lut3d_free(lut);
    return frame > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
