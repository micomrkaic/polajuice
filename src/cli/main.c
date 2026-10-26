/* The CLI uses POSIX interfaces (dirent, strdup, strcasecmp); ask for
 * their declarations explicitly since -std=c17 is strict ISO mode. */
#define _POSIX_C_SOURCE 200809L

#include "polajuice.h"
#include "filmlib.h"
#include "recipe.h"

int serve_main(int argc, char **argv);

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>
#include <sys/stat.h>

static void usage(FILE *stream)
{
    fprintf(stream,
        "Polajuice %s - photographic process emulator\n\n"
        "Usage:\n"
        "  polajuice apply INPUT -c CAMERA [-f FILM] [-o OUTPUT] [options]\n"
        "  polajuice cameras\n"
        "  polajuice describe CAMERA\n"
        "  polajuice films [NAME]\n"
        "  polajuice inspect INPUT\n"
        "  polajuice serve [DIR] [--port 8000]\n"
        "      the workspace page on http://localhost:8000, rendering here\n"
        "      on every core, with DIR's images in the strip, exports next\n"
        "      to the source, and looks kept as looks/*.recipe\n"
        "  polajuice develop INPUT [-o OUTPUT] [--ev N] [--linear]\n"
        "      the straight print of a raw: scene-linear decode, a stop up,\n"
        "      the scene shoulder, no camera and no film\n\n"
        "A look is a CAMERA (optics, grain, framing, process) plus a FILM\n"
        "(color chemistry as a .cube 3D LUT). Each camera has a canonical\n"
        "film used automatically when installed; -f overrides it.\n\n"
        "Options:\n"
        "  -c, --camera NAME       camera/format archetype (see 'cameras')\n"
        "  -f, --film NAME|FILE    film stock: a name searched in the film\n"
        "                          library, or a path to a .cube file\n"
        "      --no-film           force the built-in scalar color engine\n"
        "      --any-film          bypass camera/film compatibility checks\n"
        "      --print NAME|FILE   print/scan stock .cube chained after the\n"
        "                          film (cinema negative->print model)\n"
        "  -o, --output PATH       output path; default INPUT_CAMERA.ext\n"
        "                          formats by extension: .ppm .png .jpg .jpeg\n"
        "                          (input also .nef .dng, or --linear)\n"
        "      --strength NUMBER   blend strength from 0 to 1 (default 1)\n"
        "      --develop MODE      development: normal (default), push+N or\n"
        "                          pull-N stops (-3..+4, halves allowed:\n"
        "                          push+1.5), cross (E-6 in C-41), or\n"
        "                          cross+N; not applicable to instant cameras\n"
        "  Scene adjustments, on the light before the camera (all 0 = off):\n"
        "      --temp STOPS        white balance warm(+)/cool(-), -2..2\n"
        "      --tint STOPS        magenta(+)/green(-), -1..1\n"
        "      --black LEVEL       black point: crush(+)/fog(-), -0.1..0.1\n"
        "      --shadows STOPS     lift(+)/drop(-) the shadows, -2..2\n"
        "      --highlights N      raw shoulder knee: softer(-)/harder(+), -1..1\n"
        "      --contrast N        scene contrast about middle gray, -1..1\n"
        "      --grad STOPS        graduated ND density at the dark edge, 0..3\n"
        "      --gradangle DEG     dark edge: 0 top, 90 right, 180 bottom, 270 left\n"
        "      --gradpos OFFSET    transition center, -0.5..0.5 of the frame\n"
        "      --crop MODE         camera (default): the camera's own format\n"
        "                          cut from the middle (square, instant\n"
        "                          print, Super 8 gate); full: keep the whole\n"
        "                          input frame at its own aspect\n"
        "      --age NUMBER        storage aging from 0 (fresh) to 1\n"
        "                          (fog, faded contrast, magenta drift)\n"
        "      --grain MODEL       classic (default) or silver: grain formed\n"
        "                          from exposure inside the film, coarse in\n"
        "                          the shadows and fine in the highlights\n"
        "      --edge NUMBER       development edge effects 0..2 (0 off,\n"
        "                          1 dilute developer, 2 stand development)\n"
        "      --seed INTEGER      deterministic grain seed\n"
        "      --ev NUMBER         exposure in stops, -4..+4 (default 0)\n"
        "      --halation NUMBER   scale the camera's halation 0..2 (0 off,\n"
        "                          default 1)\n"
        "      --linear            take a 16-bit PNG/TIFF as scene-linear\n"
        "                          (dcraw -4 -T, darktable linear export)\n"
        "      --recipe FILE       take the look from a recipe: a key=value\n"
        "                          file, or any JPEG polajuice rendered (the\n"
        "                          recipe rides in it); flags given after\n"
        "                          override it line by line\n"
        "      --save-recipe FILE  write the look used as a recipe file\n"
        "  -h, --help              show this help\n\n",
        pj_version_string());
    fputs(
        "Raw input: .nef and .dng files open through a scene-linear door -\n"
        "the sensor's white sits a stop above display white and the top stop\n"
        "rolls in through a shoulder, so halation and the film curve see real\n"
        "highlights. JPEG/PNG input renders exactly as before.\n\n"
        "Film library: $POLAJUICE_FILMS, else ./data/luts ('make fetch-luts').\n",
        stream);
}

static int fail(const PjError *error)
{
    fprintf(stderr, "polajuice: %s\n", error->message);
    return EXIT_FAILURE;
}

static int list_cameras(void)
{
    for (size_t i = 0; i < pj_preset_count(); ++i) {
        const char *name = pj_preset_name(i);
        const char *film = pj_preset_default_film(name);
        char traits[512];
        char processes[256];
        pj_preset_film_processes(name, processes, sizeof processes);
        printf("%-21s %s\n%-21s   traits: %s\n%-21s   films: %s\n"
               "%-21s   film: %s\n", name,
               pj_preset_description(name), "",
               pj_preset_traits(name, traits, sizeof traits), "",
               processes[0] ? processes : "none (sealed process)", "",
               film ? film : "(built-in scalar engine)");
    }
    return EXIT_SUCCESS;
}

static int inspect_image(const char *path)
{
    PjError error = {{0}};
    char *embedded = pj_jpeg_read_comment(path);
    PjImage *image = pj_image_load(path, &error);
    if (!image) return fail(&error);
    bool raw = pj_image_is_scene_linear(image);
    const float *px = pj_image_pixels_const(image);
    size_t n = pj_image_width(image) * pj_image_height(image);
    size_t clipped = 0;
    double mean = 0.0;
    for (size_t i = 0; i < n; ++i) {
        float y = 0.2126f * px[i * 3] + 0.7152f * px[i * 3 + 1] + 0.0722f * px[i * 3 + 2];
        mean += y;
        if (px[i * 3] >= 0.999f || px[i * 3 + 1] >= 0.999f || px[i * 3 + 2] >= 0.999f) ++clipped;
    }
    mean = n ? mean / (double)n : 0.0;
    printf("path: %s\nwidth: %zu\nheight: %zu\ncolorspace: %s\n",
           path, pj_image_width(image), pj_image_height(image),
           raw ? "scene-linear (sensor white = 1.0)" : "sRGB");
    if (embedded) {
        printf("recipe: embedded (replay with --recipe %s)\n%s", path, embedded);
        free(embedded);
    }
    if (raw)
        printf("raw: %s\n"
               "mean luminance: %.3f (%.2f stops below sensor white)\n"
               "clipped: %.2f%% of pixels at sensor white\n",
               pj_raw_description(),
               mean, mean > 0.0 ? -log2(mean) : 0.0,
               100.0 * (double)clipped / (double)(n ? n : 1));
    pj_image_free(image);
    return EXIT_SUCCESS;
}

/* ---- develop ---------------------------------------------------------- */

static int develop(int argc, char **argv)
{
    const char *input = argv[2];
    const char *output = NULL;
    float ev = 0.0f;
    bool linear_input = false;
    for (int i = 3; i < argc; ++i) {
        if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--ev") && i + 1 < argc) {
            char *end = NULL;
            ev = strtof(argv[++i], &end);
            if (!end || *end || ev < -4.0f || ev > 4.0f) {
                fprintf(stderr, "invalid ev (-4..4)\n");
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[i], "--linear")) linear_input = true;
        else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    char derived[4096];
    if (!output) {
        const char *dot = strrchr(input, '.');
        const char *slash = strrchr(input, '/');
        if (!dot || (slash && dot < slash)) dot = input + strlen(input);
        int n = snprintf(derived, sizeof derived, "%.*s_straight.jpg",
                         (int)(dot - input), input);
        if (n < 0 || (size_t)n >= sizeof derived) {
            fprintf(stderr, "input path too long\n");
            return EXIT_FAILURE;
        }
        output = derived;
    }
    PjError error = {{0}};
    PjImage *image = linear_input ? pj_image_load_linear(input, &error)
                                  : pj_image_load(input, &error);
    if (!image) return fail(&error);
    if (!pj_image_is_scene_linear(image))
        fprintf(stderr, "note: %s is display-referred; written back as is\n", input);
    pj_image_scene_to_display(image, ev);
    bool saved = pj_image_save(image, output, &error);
    pj_image_free(image);
    return saved ? EXIT_SUCCESS : fail(&error);
}

/* ---- apply ------------------------------------------------------------ */

static int apply(int argc, char **argv)
{
    const char *input = argc > 2 ? argv[2] : NULL;
    const char *output = NULL;
    const char *camera = NULL;
    const char *film_request = NULL;
    const char *print_request = NULL;
    bool no_film = false;
    bool linear_input = false;
    bool any_film = false;
    const char *save_recipe = NULL;
    PjRenderOptions options = {.seed = UINT64_C(0x504f4c414a554943),
                               .strength = 1.0f};

    /* the recipe goes first so that flags override it */
    static Recipe recipe;
    for (int i = 3; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--recipe")) {
            if (!recipe_load(argv[i + 1], &recipe, "polajuice")) return EXIT_FAILURE;
            if (!recipe_apply(&recipe, &options, &camera, &film_request, &no_film,
                              &print_request, "polajuice"))
                return EXIT_FAILURE;
            break;
        }

    for (int i = 3; i < argc; ++i) {
        /* -p/--preset and --lut are accepted as quiet aliases from 0.3.x */
        if ((!strcmp(argv[i], "-c") || !strcmp(argv[i], "--camera") ||
             !strcmp(argv[i], "-p") || !strcmp(argv[i], "--preset")) &&
            i + 1 < argc)
            camera = argv[++i];
        else if ((!strcmp(argv[i], "-f") || !strcmp(argv[i], "--film") ||
                  !strcmp(argv[i], "--lut")) && i + 1 < argc)
            film_request = argv[++i];
        else if (!strcmp(argv[i], "--no-film"))
            no_film = true;
        else if (!strcmp(argv[i], "--any-film"))
            any_film = true;
        else if (!strcmp(argv[i], "--print") && i + 1 < argc)
            print_request = argv[++i];
        else if (!strcmp(argv[i], "--develop") && i + 1 < argc) {
            const char *dev = argv[++i];
            if (!pj_develop_parse(dev, &options.push, &options.cross_process)) {
                fprintf(stderr, "invalid develop mode '%s' "
                        "(normal, push+N, pull-N, cross, cross+N; N in %g..%g stops)\n",
                        dev, (double)PJ_PUSH_MIN, (double)PJ_PUSH_MAX);
                return EXIT_FAILURE;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == '-' && pj_scene_is_key(argv[i] + 2) && i + 1 < argc) {
            const char *key = argv[i] + 2;
            if (!pj_scene_set(&options, key, argv[++i])) {
                size_t n; const PjSceneKey *k = pj_scene_keys(&n);
                for (size_t j = 0; j < n; ++j)
                    if (!strcmp(k[j].key, key))
                        fprintf(stderr, "invalid --%s '%s' (%g..%g)\n", key, argv[i], (double)k[j].lo, (double)k[j].hi);
                return EXIT_FAILURE;
            }
        }
        else if (!strcmp(argv[i], "--crop") && i + 1 < argc) {
            options.crop = pj_crop_parse(argv[++i]);
            if (options.crop < 0) {
                fprintf(stderr, "invalid crop mode '%s' (camera, full)\n", argv[i]);
                return EXIT_FAILURE;
            }
        }
        else if (!strcmp(argv[i], "--age") && i + 1 < argc) {
            char *end = NULL;
            options.age = strtof(argv[++i], &end);
            if (!end || *end || options.age < 0.0f || options.age > 1.0f) {
                fprintf(stderr, "invalid age (0..1)\n");
                return EXIT_FAILURE;
            }
        }
        else if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--strength") && i + 1 < argc) {
            char *end = NULL;
            options.strength = strtof(argv[++i], &end);
            if (!end || *end) { fprintf(stderr, "invalid strength\n"); return EXIT_FAILURE; }
        } else if (!strcmp(argv[i], "--filter") && i + 1 < argc) {
            options.contrast_filter = argv[++i];
            if (!pj_contrast_filter_known(options.contrast_filter)) {
                fprintf(stderr, "polajuice: unknown filter '%s'; the rack "
                        "holds: yellow, orange, red, green, blue, none\n",
                        options.contrast_filter);
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[i], "--grain") && i + 1 < argc) {
            options.grain_model = argv[++i];
            if (!pj_grain_model_known(options.grain_model)) {
                fprintf(stderr, "polajuice: unknown grain model '%s' "
                        "(classic, silver)\n", options.grain_model);
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[i], "--ev") && i + 1 < argc) {
            char *end = NULL;
            options.ev = strtof(argv[++i], &end);
            if (!end || *end || options.ev < -4.0f || options.ev > 4.0f) {
                fprintf(stderr, "invalid ev (-4..4)\n");
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[i], "--halation") && i + 1 < argc) {
            char *end = NULL;
            float h = strtof(argv[++i], &end);
            if (!end || *end || h < 0.0f || h > 2.0f) {
                fprintf(stderr, "invalid halation (0..2)\n");
                return EXIT_FAILURE;
            }
            options.halation = h == 0.0f ? PJ_HALATION_OFF : h;
        } else if (!strcmp(argv[i], "--linear")) {
            linear_input = true;
        } else if (!strcmp(argv[i], "--recipe") && i + 1 < argc) {
            ++i;   /* applied above */
        } else if (!strcmp(argv[i], "--save-recipe") && i + 1 < argc) {
            save_recipe = argv[++i];
        } else if (!strcmp(argv[i], "--edge") && i + 1 < argc) {
            char *end = NULL;
            options.edge = strtof(argv[++i], &end);
            if (!end || *end || options.edge < 0.0f || options.edge > 2.0f) {
                fprintf(stderr, "invalid edge (0..2)\n");
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *end = NULL;
            errno = 0;
            options.seed = strtoull(argv[++i], &end, 0);
            if (errno || !end || *end) { fprintf(stderr, "invalid seed\n"); return EXIT_FAILURE; }
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    if (!input || !camera) {
        usage(stderr);
        return EXIT_FAILURE;
    }
    if (no_film && film_request) {
        if (recipe.has_film) no_film = false;   /* -f after a film=none recipe */
        else {
            fprintf(stderr, "--film and --no-film are mutually exclusive\n");
            return EXIT_FAILURE;
        }
    }
    char derived[4096];
    if (!output) {
        const char *dot = strrchr(input, '.');
        const char *slash = strrchr(input, '/');
        if (!dot || (slash && dot < slash)) dot = input + strlen(input);
        /* a raw or linear input cannot be written back in its own
         * format; those derive a .jpg */
        const char *ext = dot;
        if (linear_input || !strcasecmp(dot, ".nef") || !strcasecmp(dot, ".dng") ||
            !strcasecmp(dot, ".tif") || !strcasecmp(dot, ".tiff"))
            ext = ".jpg";
        int n = snprintf(derived, sizeof derived, "%.*s_%s%s",
                         (int)(dot - input), input, camera, ext);
        if (n < 0 || (size_t)n >= sizeof derived) {
            fprintf(stderr, "input path too long\n");
            return EXIT_FAILURE;
        }
        output = derived;
    }

    if ((options.push != 0.0f || options.cross_process) &&
        pj_preset_is_instant(camera))
        fprintf(stderr, "note: %s develops inside the film unit; "
                "--develop is ignored for instant cameras\n", camera);
    if (options.edge > 0.0f && pj_preset_is_instant(camera))
        fprintf(stderr, "note: %s develops inside the film unit; "
                "--edge is ignored for instant cameras\n", camera);

    /* Choose the film: explicit request > camera default > scalar engine. */
    char *film_path = NULL;
    if (film_request) {
        char processes[256];
        if (pj_preset_film_processes(camera, processes, sizeof processes) &&
            processes[0] == '\0' && !any_film) {
            fprintf(stderr,
                    "polajuice: %s is a sealed process and takes no film "
                    "(the plate, cassette or dye-transfer process is the medium); "
                    "use --any-film to override\n", camera);
            return EXIT_FAILURE;
        }
        film_path = resolve_film(film_request, false);
        if (!film_path) return EXIT_FAILURE;
        const char *process = film_process_of(film_path);
        if (process && !any_film &&
            !pj_preset_accepts_film(camera, process)) {
            fprintf(stderr,
                    "polajuice: %s takes %s; '%s' is %s film.\n"
                    "  (slide stock in negative chemistry is what "
                    "--develop cross emulates; --any-film overrides)\n",
                    camera, processes[0] ? processes : "no film",
                    film_request, process);
            free(film_path);
            return EXIT_FAILURE;
        }
    } else if (!no_film) {
        const char *canonical = pj_preset_default_film(camera);
        if (canonical) {
            film_path = resolve_film(canonical, true);
            if (!film_path)
                fprintf(stderr,
                        "note: canonical film '%s' not installed; using the "
                        "built-in scalar engine ('make fetch-luts' installs "
                        "the film library)\n",
                        canonical);
        }
    }

    PjError error = {{0}};
    PjLut3D *lut = NULL;
    if (film_path) {
        options.film_process = film_process_of(film_path);
        {
            const char *slash = strrchr(film_path, '/');
            static char stem_buffer[256];
            snprintf(stem_buffer, sizeof stem_buffer, "%s",
                     slash ? slash + 1 : film_path);
            char *dot = strrchr(stem_buffer, '.');
            if (dot) *dot = '\0';
            options.film_stem = stem_buffer;
        }
        lut = pj_lut3d_load_cube(film_path, &error);
        free(film_path);
        if (!lut) return fail(&error);
        options.color_lut = lut;
    }
    PjLut3D *print_lut = NULL;
    if (print_request) {
        char *print_path = resolve_film(print_request, false);
        if (!print_path) { pj_lut3d_free(lut); return EXIT_FAILURE; }
        print_lut = pj_lut3d_load_cube(print_path, &error);
        free(print_path);
        if (!print_lut) { pj_lut3d_free(lut); return fail(&error); }
        options.print_lut = print_lut;
    }
    if (!options.film_process)
        options.film_process = pj_preset_primary_process(camera);

    static char recipe_text[1024];
    {
        char print_stem[256];
        const char *pstem = NULL;
        if (print_request) {
            char *print_path = resolve_film(print_request, true);
            pstem = film_stem_of_path(print_path, print_stem, sizeof print_stem);
            free(print_path);
        }
        pj_recipe_format(camera, lut ? options.film_stem : "none", pstem,
                         &options, recipe_text, sizeof recipe_text);
        if (save_recipe) {
            FILE *rf = fopen(save_recipe, "w");
            if (!rf || fputs(recipe_text, rf) == EOF || fclose(rf) != 0) {
                fprintf(stderr, "polajuice: cannot write recipe '%s'\n", save_recipe);
                pj_lut3d_free(lut); pj_lut3d_free(print_lut);
                return EXIT_FAILURE;
            }
        }
    }

    const char *prof = getenv("POLAJUICE_PROFILE");
    bool profiling = prof && *prof && *prof != '0';
    struct timespec t0, t1;
    if (profiling) {
        timespec_get(&t0, TIME_UTC);
        fprintf(stderr, "polajuice profile (%u threads)\n", pj_thread_count());
    }
    PjImage *source = linear_input ? pj_image_load_linear(input, &error)
                                   : pj_image_load(input, &error);
    if (!source) {
        pj_lut3d_free(lut);
        pj_lut3d_free(print_lut);
        return fail(&error);
    }
    if (profiling) {
        timespec_get(&t1, TIME_UTC);
        fprintf(stderr, "  %-22s %6.1f ms\n", "load (decode)",
                ((double)t1.tv_sec - (double)t0.tv_sec) * 1000.0 + ((double)t1.tv_nsec - (double)t0.tv_nsec) / 1e6);
    }
    PjGeometry geom;
    pj_render_geometry_opt(camera, pj_image_width(source), pj_image_height(source), &options, &geom);
    PjImage *result = pj_render(source, camera, &options, &error);
    pj_image_free(source);
    pj_lut3d_free(lut);
    pj_lut3d_free(print_lut);
    if (!result) return fail(&error);
    {
        /* the JPEG comment carries the recipe and, after it, where the
         * picture landed (a comment line, so recipe readers skip it) */
        char comment[1200];
        size_t n = (size_t)snprintf(comment, sizeof comment, "%s", recipe_text);
        if (n < sizeof comment) pj_geometry_format(&geom, comment + n, sizeof comment - n);
        pj_image_set_comment(result, comment);
    }
    if (profiling) timespec_get(&t0, TIME_UTC);
    bool saved = pj_image_save(result, output, &error);
    if (profiling) {
        timespec_get(&t1, TIME_UTC);
        fprintf(stderr, "  %-22s %6.1f ms\n", "save (encode)",
                ((double)t1.tv_sec - (double)t0.tv_sec) * 1000.0 + ((double)t1.tv_nsec - (double)t0.tv_nsec) / 1e6);
    }
    pj_image_free(result);
    return saved ? EXIT_SUCCESS : fail(&error);
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage(argc < 2 ? stderr : stdout);
        return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (!strcmp(argv[1], "apply"))
        return apply(argc, argv);
    if (!strcmp(argv[1], "cameras") || !strcmp(argv[1], "presets"))
        return list_cameras();
    if (!strcmp(argv[1], "films"))
        return list_films(argc > 2 ? argv[2] : NULL);
    if (!strcmp(argv[1], "describe") && argc > 2) {
        const char *description = pj_preset_description(argv[2]);
        if (!description) {
            fprintf(stderr, "unknown camera: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
        char traits[512];
        const char *film = pj_preset_default_film(argv[2]);
        char processes[256];
        pj_preset_film_processes(argv[2], processes, sizeof processes);
        printf("%s: %s\n  traits: %s\n  films: %s\n  canonical film: %s\n",
               argv[2], description,
               pj_preset_traits(argv[2], traits, sizeof traits),
               processes[0] ? processes : "none (sealed process)",
               film ? film : "(built-in scalar engine)");
        return EXIT_SUCCESS;
    }
    if (!strcmp(argv[1], "develop") && argc > 2)
        return develop(argc, argv);
    if (!strcmp(argv[1], "serve"))
        return serve_main(argc, argv);
    if (!strcmp(argv[1], "inspect") && argc > 2)
        return inspect_image(argv[2]);
    usage(stderr);
    return EXIT_FAILURE;
}
