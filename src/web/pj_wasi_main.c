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
        char tokens[128];
        tokens[0] = '\0';
        {
            static const char *all[] = {"slide", "negative", "bw",
                                        "integral", "pack"};
            size_t used = 0;
            for (size_t t = 0; t < 5; ++t)
                if (pj_preset_accepts_film(name, all[t])) {
                    int n = snprintf(tokens + used, sizeof tokens - used,
                                     "%s%s", used ? "," : "", all[t]);
                    if (n > 0) used += (size_t)n;
                }
        }
        printf("%s\t%s\t%s\t%s\t%s\t%s\n", name, film ? film : "",
               pj_preset_description(name),
               pj_preset_traits(name, traits, sizeof traits),
               pj_preset_is_instant(name) ? "instant" : "film", tokens);
    }
    return 0;
}

static int render(int argc, char **argv)
{
    if (argc != 15) {
        fprintf(stderr, "args: render IN CAMERA FILM|- PROCESS|- FILMSTEM|- "
                        "PRINT|- FILTER|- STRENGTH SEED AGE DEVELOP MAX_DIM "
                        "OUT\n");
        return 2;
    }
    const char *in_path = argv[2], *camera = argv[3], *film = argv[4];
    const char *process = argv[5];
    const char *film_stem = argv[6];
    const char *print_path = argv[7];
    const char *filter = argv[8];
    float strength = strtof(argv[9], NULL);
    uint64_t seed = strtoull(argv[10], NULL, 0);
    float age = strtof(argv[11], NULL);
    const char *dev = argv[12];
    size_t max_dim = (size_t)strtoull(argv[13], NULL, 0);
    const char *out_path = argv[14];

    /* Engine-level compatibility parity with the CLI: the browser UI
     * filters too, but the artifact itself must refuse nonsense. An
     * unknown process ("-") is never blocked, matching path-supplied
     * cubes on the CLI. */
    if (strcmp(film, "-")) {
        char processes[256];
        pj_preset_film_processes(camera, processes, sizeof processes);
        if (processes[0] == '\0') {
            fprintf(stderr, "%s is a sealed process and takes no film\n",
                    camera);
            return 1;
        }
        if (strcmp(process, "-") &&
            !pj_preset_accepts_film(camera, process)) {
            fprintf(stderr, "%s takes %s; this stock is %s film\n",
                    camera, processes, process);
            return 1;
        }
    }

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
    PjLut3D *print_lut = NULL;
    if (strcmp(print_path, "-")) {
        print_lut = pj_lut3d_load_cube(print_path, &error);
        if (!print_lut) {
            fprintf(stderr, "%s\n", error.message);
            pj_lut3d_free(lut);
            return 1;
        }
    }
    PjRenderOptions options = {.seed = seed, .strength = strength,
                               .age = age, .color_lut = lut,
                               .print_lut = print_lut,
                               .film_process =
                                   strcmp(process, "-")
                                       ? process
                                       : pj_preset_primary_process(camera),
                               .film_stem =
                                   strcmp(film_stem, "-") ? film_stem : NULL,
                               .contrast_filter =
                                   strcmp(filter, "-") ? filter : NULL};
    if (!strcmp(dev, "push+1")) options.push = 1.0f;
    else if (!strcmp(dev, "push+2")) options.push = 2.0f;
    else if (!strcmp(dev, "pull-1")) options.push = -1.0f;
    else if (!strcmp(dev, "cross")) options.cross_process = true;
    PjImage *result = pj_render(image, camera, &options, &error);
    pj_image_free(image);
    pj_lut3d_free(lut);
    pj_lut3d_free(print_lut);
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
