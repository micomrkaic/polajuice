#include "polajuice.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *stream)
{
    fprintf(stream,
        "Polajuice %s - photographic process emulator\n\n"
        "Usage:\n"
        "  polajuice apply INPUT -p PRESET [-o OUTPUT] [options]\n"
        "  polajuice presets\n"
        "  polajuice describe PRESET\n"
        "  polajuice inspect INPUT\n\n"
        "Options:\n"
        "  -p, --preset NAME       select a photographic recipe\n"
        "  -o, --output PATH       output path; default INPUT_PRESET.ext\n"
        "                          formats by extension: .ppm .png .jpg .jpeg\n"
        "      --strength NUMBER   blend strength from 0 to 1 (default 1)\n"
        "      --seed INTEGER      deterministic grain seed\n"
        "      --lut FILE.cube     apply a standard 3D color LUT\n"
        "  -h, --help              show this help\n",
        pj_version_string());
}

static int fail(const PjError *error)
{
    fprintf(stderr, "polajuice: %s\n", error->message);
    return EXIT_FAILURE;
}

static int list_presets(void)
{
    for (size_t i = 0; i < pj_preset_count(); ++i) {
        const char *name = pj_preset_name(i);
        printf("%-21s %s\n", name, pj_preset_description(name));
    }
    return EXIT_SUCCESS;
}

static int inspect_image(const char *path)
{
    PjError error = {{0}};
    PjImage *image = pj_image_load(path, &error);
    if (!image) return fail(&error);
    printf("path: %s\nwidth: %zu\nheight: %zu\ncolorspace: sRGB\n",
           path, pj_image_width(image), pj_image_height(image));
    pj_image_free(image);
    return EXIT_SUCCESS;
}

static int apply(int argc, char **argv)
{
    const char *input = argc > 2 ? argv[2] : NULL;
    const char *output = NULL;
    const char *preset = NULL;
    const char *lut_path = NULL;
    PjRenderOptions options = {.seed = UINT64_C(0x504f4c414a554943),
                               .strength = 1.0f};

    for (int i = 3; i < argc; ++i) {
        if ((!strcmp(argv[i], "-p") || !strcmp(argv[i], "--preset")) && i + 1 < argc)
            preset = argv[++i];
        else if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--strength") && i + 1 < argc) {
            char *end = NULL;
            options.strength = strtof(argv[++i], &end);
            if (!end || *end) { fprintf(stderr, "invalid strength\n"); return EXIT_FAILURE; }
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *end = NULL;
            errno = 0;
            options.seed = strtoull(argv[++i], &end, 0);
            if (errno || !end || *end) { fprintf(stderr, "invalid seed\n"); return EXIT_FAILURE; }
        } else if (!strcmp(argv[i], "--lut") && i + 1 < argc) {
            lut_path = argv[++i];
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    if (!input || !preset) {
        usage(stderr);
        return EXIT_FAILURE;
    }
    char derived[4096];
    if (!output) {
        const char *dot = strrchr(input, '.');
        const char *slash = strrchr(input, '/');
        if (!dot || (slash && dot < slash)) dot = input + strlen(input);
        int n = snprintf(derived, sizeof derived, "%.*s_%s%s",
                         (int)(dot - input), input, preset, dot);
        if (n < 0 || (size_t)n >= sizeof derived) {
            fprintf(stderr, "input path too long\n");
            return EXIT_FAILURE;
        }
        output = derived;
    }

    PjError error = {{0}};
    PjLut3D *lut = NULL;
    if (lut_path) {
        lut = pj_lut3d_load_cube(lut_path, &error);
        if (!lut) return fail(&error);
        options.color_lut = lut;
    }
    PjImage *source = pj_image_load(input, &error);
    if (!source) {
        pj_lut3d_free(lut);
        return fail(&error);
    }
    PjImage *result = pj_render(source, preset, &options, &error);
    pj_image_free(source);
    pj_lut3d_free(lut);
    if (!result) return fail(&error);
    bool saved = pj_image_save(result, output, &error);
    pj_image_free(result);
    return saved ? EXIT_SUCCESS : fail(&error);
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(stderr); return EXIT_FAILURE; }
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage(stdout); return EXIT_SUCCESS;
    }
    if (!strcmp(argv[1], "presets")) return list_presets();
    if (!strcmp(argv[1], "describe")) {
        if (argc != 3) { usage(stderr); return EXIT_FAILURE; }
        const char *description = pj_preset_description(argv[2]);
        if (!description) { fprintf(stderr, "unknown preset: %s\n", argv[2]); return EXIT_FAILURE; }
        printf("%s: %s\n", argv[2], description);
        return EXIT_SUCCESS;
    }
    if (!strcmp(argv[1], "inspect")) {
        if (argc != 3) { usage(stderr); return EXIT_FAILURE; }
        return inspect_image(argv[2]);
    }
    if (!strcmp(argv[1], "apply")) return apply(argc, argv);
    usage(stderr);
    return EXIT_FAILURE;
}
