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
 *   polajuice.wasm render IN CAMERA FILM|- PROCESS|- FILMSTEM|- PRINT|-
 *                         FILTER|- STRENGTH SEED AGE DEVELOP MAX_DIM OUT
 *                         GRAIN EDGE EV HALATION PRINTSTEM|- CROP SCENE|-
 *       SCENE: "temp=0.3,grad=1.5,..." scene adjustments, or - for none
 *   polajuice.wasm develop IN MAX_DIM OUT
 *       the straight print of a raw (.nef/.dng) input: scene-linear
 *       decode, a stop up, shoulder, JPEG - what the page shows as the
 *       "original" when the browser itself cannot decode the file.
 *       FILM "-" selects the built-in scalar engine; MAX_DIM 0 renders
 *       at full resolution, otherwise the input is box-average
 *       downscaled in linear light first (preview mode). GRAIN is
 *       classic or silver; EDGE the development edge amount 0..2.
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
        const char *format = pj_preset_format(name);
        printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\n", name, film ? film : "",
               pj_preset_description(name),
               pj_preset_traits(name, traits, sizeof traits),
               pj_preset_is_instant(name) ? "instant" : "film", tokens,
               format ? format : "");
    }
    return 0;
}

static int render(int argc, char **argv)
{
    if (argc != 22) {
        fprintf(stderr, "args: render IN CAMERA FILM|- PROCESS|- FILMSTEM|- "
                        "PRINT|- FILTER|- STRENGTH SEED AGE DEVELOP MAX_DIM "
                        "OUT GRAIN EDGE EV HALATION PRINTSTEM|- CROP SCENE|-\n");
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
    const char *grain_model = argv[15];
    float edge = strtof(argv[16], NULL);
    if (!pj_grain_model_known(grain_model)) {
        fprintf(stderr, "unknown grain model '%s' (classic, silver)\n",
                grain_model);
        return 2;
    }
    if (edge < 0.0f || edge > 2.0f) {
        fprintf(stderr, "edge must be 0..2\n");
        return 2;
    }
    float ev = strtof(argv[17], NULL);
    float halation = strtof(argv[18], NULL);
    const char *print_stem = strcmp(argv[19], "-") ? argv[19] : NULL;
    if (ev < -4.0f || ev > 4.0f) { fprintf(stderr, "ev must be -4..4\n"); return 2; }
    if (halation < 0.0f || halation > 2.0f) { fprintf(stderr, "halation must be 0..2\n"); return 2; }

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
                                   strcmp(filter, "-") ? filter : NULL,
                               .grain_model = grain_model,
                               .edge = edge,
                               .ev = ev,
                               .halation = halation == 0.0f ? PJ_HALATION_OFF
                                                            : halation};
    if (!pj_develop_parse(dev, &options.push, &options.cross_process)) {
        pj_image_free(image); pj_lut3d_free(lut); pj_lut3d_free(print_lut);
        fprintf(stderr, "unknown develop mode '%s'\n", dev);
        return 1;
    }
    options.crop = pj_crop_parse(argv[20]);
    if (options.crop < 0) {
        pj_image_free(image); pj_lut3d_free(lut); pj_lut3d_free(print_lut);
        fprintf(stderr, "unknown crop mode '%s'\n", argv[20]);
        return 1;
    }
    /* scene adjustments as "key=value,key=value" ("-" for none) */
    if (strcmp(argv[21], "-")) {
        char buf[512];
        snprintf(buf, sizeof buf, "%s", argv[21]);
        /* a hand-rolled split: WASI libc builds strictly, without strtok_r */
        char *tok = buf;
        while (tok && *tok) {
            char *comma = strchr(tok, ',');
            if (comma) *comma = '\0';
            char *next = comma ? comma + 1 : NULL;
            char *eq = strchr(tok, '=');
            if (!eq) { tok = next; continue; }
            *eq = '\0';
            if (!pj_scene_set(&options, tok, eq + 1)) {
                pj_image_free(image); pj_lut3d_free(lut); pj_lut3d_free(print_lut);
                fprintf(stderr, "scene: %s=%s out of range\n", tok, eq + 1);
                return 1;
            }
            tok = next;
        }
    }
    PjGeometry geom;
    pj_render_geometry_opt(camera, pj_image_width(image), pj_image_height(image), &options, &geom);
    PjImage *result = pj_render(image, camera, &options, &error);
    pj_image_free(image);
    pj_lut3d_free(lut);
    pj_lut3d_free(print_lut);
    if (!result) { fprintf(stderr, "%s\n", error.message); return 1; }
    /* the recipe rides in the JPEG, as from the CLI: the page reads it
     * back from the bytes, so the engine is the one source of its text;
     * after it, where the picture landed, for the page's wipe */
    {
        char recipe[1200];
        size_t n = pj_recipe_format(camera, lut ? options.film_stem : "none", print_stem,
                                    &options, recipe, sizeof recipe);
        if (n < sizeof recipe) pj_geometry_format(&geom, recipe + n, sizeof recipe - n);
        pj_image_set_comment(result, recipe);
    }
    bool saved = pj_image_save(result, out_path, &error);
    pj_image_free(result);
    if (!saved) { fprintf(stderr, "%s\n", error.message); return 1; }
    return 0;
}

static int develop(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "args: develop IN MAX_DIM OUT\n"); return 2; }
    PjError error = {{0}};
    PjImage *image = pj_image_load(argv[2], &error);
    if (!image) { fprintf(stderr, "%s\n", error.message); return 1; }
    image = pj_image_downscale(image, (size_t)strtoull(argv[3], NULL, 0), &error);
    if (!image) { fprintf(stderr, "%s\n", error.message); return 1; }
    pj_image_scene_to_display(image, 0.0f);
    bool saved = pj_image_save(image, argv[4], &error);
    pj_image_free(image);
    if (!saved) { fprintf(stderr, "%s\n", error.message); return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "develop")) return develop(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "--list-cameras")) return list_cameras();
    if (argc >= 2 && !strcmp(argv[1], "--version")) {
        printf("%s\n", pj_version_string());
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "render")) return render(argc, argv);
    fprintf(stderr, "modes: --list-cameras | --version | render ... | develop ...\n");
    return 2;
}
