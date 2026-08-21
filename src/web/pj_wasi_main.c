/*
 * WASI command entry point: the same module serves the browser (through
 * the vendored browser_wasi_shim, which supplies an in-memory filesystem
 * and captures stdout) and any WASI runtime such as node's, which is how
 * this exact binary is tested against the native CLI for byte-identical
 * output before it ships.
 *
 * Modes:
 *   polajuice.wasm --list-cameras
 *       prints "name<TAB>default_film<TAB>description" per camera
 *   polajuice.wasm --version
 *   polajuice.wasm render IN CAMERA FILM|- STRENGTH SEED AGE DEVELOP MAX_DIM OUT
 *       FILM "-" selects the built-in scalar engine; MAX_DIM 0 renders
 *       at full resolution, otherwise the input is box-average
 *       downscaled in linear light first (preview mode).
 */
#include "polajuice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int list_cameras(void)
{
    for (size_t i = 0; i < pj_preset_count(); ++i) {
        const char *name = pj_preset_name(i);
        const char *film = pj_preset_default_film(name);
        char traits[512];
        printf("%s\t%s\t%s\t%s\t%s\n", name, film ? film : "",
               pj_preset_description(name),
               pj_preset_traits(name, traits, sizeof traits),
               pj_preset_is_instant(name) ? "instant" : "film");
    }
    return 0;
}

static int render(int argc, char **argv)
{
    if (argc != 11) {
        fprintf(stderr, "args: render IN CAMERA FILM|- STRENGTH SEED AGE "
                        "DEVELOP MAX_DIM OUT\n");
        return 2;
    }
    const char *in_path = argv[2], *camera = argv[3], *film = argv[4];
    float strength = strtof(argv[5], NULL);
    uint64_t seed = strtoull(argv[6], NULL, 0);
    float age = strtof(argv[7], NULL);
    const char *dev = argv[8];
    size_t max_dim = (size_t)strtoull(argv[9], NULL, 0);
    const char *out_path = argv[10];

    PjError error = {{0}};
    PjLut3D *lut = NULL;
    if (strcmp(film, "-")) {
        lut = pj_lut3d_load_cube(film, &error);
        if (!lut) { fprintf(stderr, "%s\n", error.message); return 1; }
    }
    PjImage *image = pj_image_load(in_path, &error);
    if (!image) {
        pj_lut3d_free(lut);
        fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    image = pj_image_downscale(image, max_dim, &error);
    if (!image) {
        pj_lut3d_free(lut);
        fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    PjRenderOptions options = {.seed = seed, .strength = strength,
                               .age = age, .color_lut = lut};
    if (!strcmp(dev, "push+1")) options.push = 1.0f;
    else if (!strcmp(dev, "push+2")) options.push = 2.0f;
    else if (!strcmp(dev, "pull-1")) options.push = -1.0f;
    else if (!strcmp(dev, "cross")) options.cross_process = true;
    PjImage *result = pj_render(image, camera, &options, &error);
    pj_image_free(image);
    pj_lut3d_free(lut);
    if (!result) { fprintf(stderr, "%s\n", error.message); return 1; }
    bool saved = pj_image_save(result, out_path, &error);
    pj_image_free(result);
    if (!saved) { fprintf(stderr, "%s\n", error.message); return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--list-cameras")) return list_cameras();
    if (argc >= 2 && !strcmp(argv[1], "--version")) {
        printf("%s\n", pj_version_string());
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "render")) return render(argc, argv);
    fprintf(stderr, "modes: --list-cameras | --version | render ...\n");
    return 2;
}
