/* The CLI uses POSIX interfaces (dirent, strdup, strcasecmp); ask for
 * their declarations explicitly since -std=c17 is strict ISO mode. */
#define _POSIX_C_SOURCE 200809L

#include "polajuice.h"
#include "filmlib.h"
#include "heic.h"
#include "dtstyle.h"
#include "curves.h"
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
        "Polajuice %s - photographic process emulator\n"
        "\n"
        "A look is a CAMERA (optics, grain, framing, process) plus a FILM (color\n"
        "chemistry, a .cube 3D LUT). Each camera has a canonical film, used when\n"
        "it is installed; -f names another.\n"
        "\n"
        "COMMANDS\n"
        "  polajuice apply INPUT -c CAMERA [-f FILM] [-o OUTPUT] [options]\n"
        "      render a photograph through a look\n"
        "  polajuice develop INPUT [-o OUTPUT] [--ev N] [--linear]\n"
        "      the straight print of a raw: no camera, no film\n"
        "  polajuice serve [DIR] [--port 8000] [--web PATH]\n"
        "      the workspace page on http://localhost:8000, rendering here on\n"
        "      every core; DIR's images in the strip, exports next to the\n"
        "      source, looks kept as DIR/looks/*.recipe; --web: the page's folder\n"
        "  polajuice curves   -c CAMERA [look] -o NAME.svg [--vs FILM|none] [--table]\n"
        "      what the look does to color, charted and tabulated\n"
        "  polajuice lut      -c CAMERA [look] -o NAME.cube [--size 33]\n"
        "      the look's color as a 3D LUT, for any editor\n"
        "  polajuice darktable -c CAMERA [look] -o NAME [--install] [--size 33]\n"
        "      the look as a darktable style (NAME.cube + NAME.dtstyle)\n"
        "  polajuice cameras              list the cameras\n"
        "  polajuice describe CAMERA      one camera in detail\n"
        "  polajuice films [NAME]         list the film library, or what matches\n"
        "  polajuice inspect INPUT        what a file is; a render's recipe; a lens\n"
        "  polajuice paths                where films, prints and the page are found\n"
        "  polajuice -h, --help           this help ('man polajuice' has more)\n"
        "\n"
        "  [look] is everything below under THE LOOK through LENS AND FRAMING, or\n"
        "  --recipe FILE. Values are numbers unless a list of words is given;\n"
        "  0 switches an adjustment off unless said otherwise.\n"
        "\n",
        pj_version_string());
    fputs(
        "THE LOOK\n"
        "  -c, --camera NAME       the camera (required): see 'polajuice cameras'\n"
        "  -f, --film NAME|FILE    film stock: a name from the library (the whole\n"
        "                          name or a part of it) or a .cube file. Default:\n"
        "                          the camera's canonical film\n"
        "      --no-film           no film LUT: the built-in color engine\n"
        "      --any-film          allow a film of a process the camera does not\n"
        "                          take (slide film in a negative camera, say)\n"
        "      --print NAME|FILE   a print stock .cube applied after the film\n"
        "                          (shipped: synthetic_minilab, synthetic_cine_print,\n"
        "                          synthetic_archival_neutral)\n"
        "      --strength N        how much of the look, 0..1 (default 1): 0 is the\n"
        "                          input, 1 the full camera and film. Scales the\n"
        "                          camera, film, grain and --filter; not --ev,\n"
        "                          --temp and the other scene adjustments\n"
        "\n"
        "EXPOSURE AND DEVELOPMENT\n"
        "      --ev STOPS          exposure, -4..+4 (default 0). +1 doubles the\n"
        "                          light reaching the film, -1 halves it. It acts\n"
        "                          on the scene, before the film, so it moves\n"
        "                          every tone along the film's curve: brightness\n"
        "                          first, and with it contrast, saturation and\n"
        "                          what is bright enough to halate. A raw file\n"
        "                          already opens one stop up; --ev adds to that\n"
        "      --develop MODE      normal (default); push+N or pull-N, N in stops,\n"
        "                          -3..+4, halves allowed (push+1.5); cross (slide\n"
        "                          film in negative chemistry); cross+N.\n"
        "                          push+N is the look of film shot N stops under\n"
        "                          and developed N stops longer: the brightness\n"
        "                          comes out even, and what is left is more\n"
        "                          contrast, saturation and grain. A pull is the\n"
        "                          reverse. So do not add --ev -N to a push: that\n"
        "                          counts the underexposure twice.\n"
        "                          Ignored on instant cameras; cross is ignored\n"
        "                          on black and white\n"
        "      --halation N        scale the camera's halation (the glow around\n"
        "                          highlights), 0..2: 0 off, 1 as the camera has\n"
        "                          it (default), 2 double\n"
        "      --age N             storage aging, 0 (fresh, default) .. 1: fog,\n"
        "                          lost contrast, faded and drifting color\n"
        "      --grain MODEL       classic (default): grain laid over the picture;\n"
        "                          silver: grain formed from exposure inside the\n"
        "                          film, coarse in shadows, fine in highlights\n"
        "      --edge N            development edge effects, 0..2: 0 off, 1 dilute\n"
        "                          developer, 2 stand development. Not on instant\n"
        "                          cameras\n"
        "      --seed INTEGER      the grain's pattern; the same seed gives the\n"
        "                          same picture to the byte\n"
        "\n",
        stream);
    fputs(
        "THE LIGHT BEFORE THE CAMERA\n"
        "      --temp STOPS        white balance, -2..2: + warmer (red up, blue\n"
        "                          down), - cooler\n"
        "      --tint STOPS        -1..1: + magenta, - green. Shifts brightness\n"
        "                          too (+1 is about a quarter stop darker)\n"
        "      --filter NAME       a filter on the lens: yellow, orange, red,\n"
        "                          green, blue. Lightens its own color, darkens\n"
        "                          the opposite; made for black-and-white cameras\n"
        "                          (orange or red for dark skies)\n"
        "      --black LEVEL       black point, -0.1..0.1: + crushes the darkest\n"
        "                          tones to black, - lifts them to a fog\n"
        "      --shadows STOPS     -2..2: + lifts the tones under middle gray,\n"
        "                          - drops them; full effect four stops under\n"
        "      --contrast N        -1..1: + steepens, - flattens, turning about\n"
        "                          middle gray, colors kept\n"
        "      --highlights N      raw only, -1..1: where the highlights start to\n"
        "                          roll off: - sooner and softer, + later and harder\n"
        "      --recover N         raw only, 0..1: rebuild clipped highlights from\n"
        "                          the channels that did not clip (neutral and\n"
        "                          bright) instead of cutting them at white\n"
        "      --grad STOPS        graduated filter, 0..3: stops of darkening at\n"
        "                          the dark edge, fading across the frame\n"
        "      --gradangle DEG     its dark edge: 0 top (default), 90 right, 180\n"
        "                          bottom, 270 left\n"
        "      --gradpos OFFSET    where it fades, -0.5..0.5 of the frame from\n"
        "                          the middle\n"
        "      --radial STOPS      radial filter, -2..2: + lightens, - darkens\n"
        "                          inside a soft ellipse\n"
        "      --radialx X         the ellipse's center, -0.5..0.5 of the frame\n"
        "      --radialy Y         from the middle (default: the middle)\n"
        "      --radialsize R      its radius, 0.05..1 of the half-diagonal\n"
        "                          (default 0.35)\n"
        "\n"
        "LENS AND FRAMING (applied to the input first)\n"
        "      --lens auto|none    auto: a raw file's lens profile sets\n"
        "                          --distortion and --devignette where you left\n"
        "                          them at 0 ('inspect' shows the lens). Default none\n"
        "      --distortion N      -0.3..0.3: + straightens barrel distortion,\n"
        "                          - pincushion\n"
        "      --devignette STOPS  0..2: lighten the corners by this much\n"
        "      --denoise N         0..3: smooth color noise, keeping detail\n"
        "      --sharpen N         0..2: capture sharpening, for a soft raw\n"
        "      --rotate DEG        straighten, -15..15: + clockwise; the frame is\n"
        "                          trimmed to stay rectangular\n"
        "      --keystonev N       perspective, -1..1: + widens the top (a building\n"
        "                          leaning back), - the bottom\n"
        "      --keystoneh N       -1..1: + widens the left edge, - the right\n"
        "      --trimx X           trim to a rectangle, each 0..1 as a fraction of\n"
        "      --trimy Y           the frame: left edge, top edge,\n"
        "      --trimw W           width kept, height kept. No trim while W or H\n"
        "      --trimh H           is 0\n"
        "      --crop MODE         camera (default): cut the camera's own format\n"
        "                          from the middle (a square, an instant print,\n"
        "                          the Super 8 gate); full: keep the whole frame\n"
        "\n",
        stream);
    fputs(
        "FILES\n"
        "  -o, --output PATH       where to write. apply: default INPUT_CAMERA.ext\n"
        "                          beside the input; the extension picks the\n"
        "                          format: .jpg .jpeg .png .ppm\n"
        "      --recipe FILE       take the look from a recipe file, or from any\n"
        "                          JPEG polajuice rendered (it carries its recipe);\n"
        "                          options given after it override it one by one\n"
        "      --save-recipe FILE  write the look used as a recipe file\n"
        "      --linear            read a 16-bit PNG or TIFF as scene-linear\n"
        "                          (dcraw -4 -T, darktable's linear export)\n"
        "  Input: .jpg .jpeg .png .ppm, raw .nef and .dng, and .heic when\n"
        "  heif-convert, heif-dec, magick, convert or sips is installed (it is\n"
        "  converted once to NAME_from_heic.jpg beside the source).\n"
        "\n"
        "ONLY FOR curves, lut, darktable\n"
        "      --vs FILM|none      curves: overlay the same look on another film\n"
        "                          (none: the built-in color engine)\n"
        "      --table             curves: print the numbers; -o may then be left out\n"
        "      --size N            lut, darktable: lattice points a side, 2..129\n"
        "                          (default 33)\n"
        "      --install           darktable: put the files where darktable looks\n"
        "\n"
        "RAW FILES open through a scene-linear door: the sensor's white sits a\n"
        "stop above display white and the top stop rolls in through a shoulder,\n"
        "so the film's curve and halation see real highlights. --highlights,\n"
        "--recover and --lens act only on raw input.\n"
        "\n"
        "WHERE THINGS ARE FOUND, first match: $POLAJUICE_FILMS, $POLAJUICE_PRINTS,\n"
        "$POLAJUICE_WEB; $POLAJUICE_HOME/{luts,prints,web}; ./data/luts,\n"
        "./data/prints, ./web (a source tree); ~/.local/share/polajuice ('make\n"
        "install'); the build's datadir. 'polajuice paths' shows the result.\n"
        "\n"
        "ENVIRONMENT\n"
        "  POLAJUICE_THREADS=N           render threads (default: all cores; the\n"
        "                                output is the same for any number)\n"
        "  POLAJUICE_PROFILE=1           print each stage's time\n"
        "  POLAJUICE_DARKTABLE_CONFIG    darktable's configuration folder, for\n"
        "                                'darktable --install'\n"
        "\n"
        "Several controls are the same control in other coordinates (a filter\n"
        "is --ev, --temp and --tint together; --shadows is --contrast under\n"
        "middle gray): see the README's annex. Older spellings still work:\n"
        "-p/--preset for --camera, --lut for --film, 'presets' for 'cameras'.\n"
        "Exit status: 0 on success, 1 on any error.\n",
        stream);
}

/* the CLI's loader: a HEIC goes through the installed converter to a
 * JPEG beside it (NAME_from_heic.jpg) and that is what opens; the note
 * says so once, since the derived file stays */
static PjImage *load_input(const char *path, PjError *error)
{
    if (heic_is(path)) {
        char jpg[4096];
        if (!heic_convert(path, jpg, sizeof jpg, error->message, sizeof error->message))
            return NULL;
        fprintf(stderr, "note: %s converted with %s -> %s\n", path, heic_converter(), jpg);
        return pj_image_load(jpg, error);
    }
    return pj_image_load(path, error);
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
    PjImage *image = load_input(path, &error);
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
    if (raw) {
        PjRawLens lens;
        if (pj_raw_last_lens(&lens)) {
            float dist = 0.0f, fall = 0.0f;
            const char *name = pj_lens_profile(&lens, &dist, &fall);
            printf("lens: ");
            if (lens.have) printf("%g-%gmm f/%g-%g", (double)lens.min_focal, (double)lens.max_focal,
                                  (double)lens.max_ap_min, (double)lens.max_ap_max);
            else printf("(unnamed)");
            if (lens.model[0]) printf(" %s", lens.model);
            if (lens.focal > 0.0f) printf(", shot at %gmm", (double)lens.focal);
            if (lens.fnumber > 0.0f) printf(" f/%g", (double)lens.fnumber);
            printf("\n");
            if (name) printf("lens profile: %s -> --distortion %g --devignette %g (--lens auto applies them)\n",
                             name, (double)dist, (double)fall);
            else printf("lens profile: none for this lens\n");
        } else printf("lens: none recorded (a manual lens?)\n");
    }
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
                                  : load_input(input, &error);
    if (!image) return fail(&error);
    if (!pj_image_is_scene_linear(image))
        fprintf(stderr, "note: %s is display-referred; written back as is\n", input);
    pj_image_scene_to_display(image, ev);
    bool saved = pj_image_save(image, output, &error);
    pj_image_free(image);
    return saved ? EXIT_SUCCESS : fail(&error);
}

/* ---- apply ------------------------------------------------------------ */

/* 'lut' and 'darktable' take a look exactly as 'apply' does - recipe,
 * camera, film, every flag - and export it instead of rendering a
 * photograph; main() routes them through apply() with this set */
static const char *g_export;    /* NULL, "lut", "darktable" or "curves" */

static int apply(int argc, char **argv)
{
    bool output_given = false;
    const char *input = argc > 2 ? argv[2] : NULL;
    int cube_size = 33;
    bool install_style = false, curves_table_out = false;
    const char *vs_request = NULL;   /* curves: a second film to overlay; "none" = the scalar engine */
    const char *output = NULL;
    const char *camera = NULL;
    const char *film_request = NULL;
    const char *print_request = NULL;
    bool no_film = false;
    bool linear_input = false;
    bool any_film = false, lens_auto = false;
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
        else if (g_export && !strcmp(argv[i], "--size") && i + 1 < argc) {
            cube_size = atoi(argv[++i]);
            if (cube_size < 2 || cube_size > 129) { fprintf(stderr, "polajuice: --size takes 2..129 (17, 33 and 65 are usual)\n"); return EXIT_FAILURE; }
        } else if (g_export && !strcmp(g_export, "darktable") && !strcmp(argv[i], "--install"))
            install_style = true;
        else if (g_export && !strcmp(g_export, "curves") && !strcmp(argv[i], "--vs") && i + 1 < argc)
            vs_request = argv[++i];
        else if (g_export && !strcmp(g_export, "curves") && !strcmp(argv[i], "--table"))
            curves_table_out = true;
        else if (!strcmp(argv[i], "--lens") && i + 1 < argc) {
            const char *mode = argv[++i];
            if (!strcmp(mode, "auto")) lens_auto = true;
            else if (!strcmp(mode, "none")) lens_auto = false;
            else { fprintf(stderr, "polajuice: --lens takes auto or none\n"); return EXIT_FAILURE; }
        }
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
    output_given = output != NULL;
    if (g_export && !output && !(!strcmp(g_export, "curves") && curves_table_out)) {
        fprintf(stderr, "polajuice %s: name the %s\n", g_export,
                !strcmp(g_export, "lut") ? "look with -o NAME.cube"
                : !strcmp(g_export, "curves") ? "chart with -o NAME.svg (or ask for --table)"
                : "look with -o NAME (NAME.cube and NAME.dtstyle are written)");
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
    if (options.cross_process && pj_preset_is_monochrome(camera))
        fprintf(stderr, "note: %s is black and white; cross-processing has no "
                "color curves to cross and is ignored\n", camera);
    if (options.edge > 0.0f && pj_preset_is_instant(camera))
        fprintf(stderr, "note: %s develops inside the film unit; "
                "--edge is ignored for instant cameras\n", camera);

    film_prefer_camera(camera);
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

    if (g_export && !strcmp(g_export, "curves")) {
        /* the look's color as a lattice, charted; --vs overlays the same
         * look on another film (or on the scalar engine: --vs none) */
        enum { N = 33 };
        float *cube_a = malloc((size_t)N * N * N * 3 * sizeof *cube_a), *cube_b = NULL;
        char name_a[400], name_b[400];
        PjLut3D *vs_lut = NULL;
        int status = EXIT_FAILURE;
        snprintf(name_a, sizeof name_a, "%s / %s", camera, lut ? options.film_stem : "scalar engine");
        if (!cube_a || !pj_look_cube(camera, &options, N, cube_a, &error)) { fprintf(stderr, "polajuice: %s\n", cube_a ? error.message : "out of memory"); goto curves_done; }
        if (vs_request) {
            PjRenderOptions other = options;
            other.color_lut = NULL; other.film_stem = NULL;
            other.film_process = pj_preset_primary_process(camera);
            snprintf(name_b, sizeof name_b, "%s / scalar engine", camera);
            if (strcmp(vs_request, "none")) {
                char *vs_path = resolve_film(vs_request, false);
                if (!vs_path) goto curves_done;
                static char vs_stem[256];
                film_stem_of_path(vs_path, vs_stem, sizeof vs_stem);
                other.film_process = film_process_of(vs_path);
                if (!other.film_process) other.film_process = pj_preset_primary_process(camera);
                vs_lut = pj_lut3d_load_cube(vs_path, &error);
                free(vs_path);
                if (!vs_lut) { fprintf(stderr, "polajuice: %s\n", error.message); goto curves_done; }
                other.color_lut = vs_lut; other.film_stem = vs_stem;
                snprintf(name_b, sizeof name_b, "%s / %s", camera, vs_stem);
            }
            cube_b = malloc((size_t)N * N * N * 3 * sizeof *cube_b);
            if (!cube_b || !pj_look_cube(camera, &other, N, cube_b, &error)) { fprintf(stderr, "polajuice: %s\n", cube_b ? error.message : "out of memory"); goto curves_done; }
        }
        {
            CurvesLook a = {name_a, cube_a, N}, b = {name_b, cube_b, N};
            if (output_given && !curves_svg(output, &a, cube_b ? &b : NULL)) goto curves_done;
            if (output_given) printf("%s\n", output);
            if (curves_table_out) { curves_table(stdout, &a, cube_b ? &b : NULL); curves_spectral_table(stdout, &a, cube_b ? &b : NULL); }
            status = EXIT_SUCCESS;
        }
curves_done:
        free(cube_a); free(cube_b);
        pj_lut3d_free(vs_lut); pj_lut3d_free(lut); pj_lut3d_free(print_lut);
        return status;
    }
    if (g_export) {
        static char look_text[1024];
        char print_stem[256];
        const char *pstem = NULL;
        if (print_request) {
            char *print_path = resolve_film(print_request, true);
            pstem = film_stem_of_path(print_path, print_stem, sizeof print_stem);
            free(print_path);
        }
        pj_recipe_format(camera, lut ? options.film_stem : "none", pstem, &options, look_text, sizeof look_text);
        int status = !strcmp(g_export, "lut")
                   ? export_cube(camera, &options, look_text, output, cube_size)
                   : export_darktable(camera, &options, look_text, output, cube_size, install_style);
        pj_lut3d_free(lut); pj_lut3d_free(print_lut);
        return status;
    }

    const char *prof = getenv("POLAJUICE_PROFILE");
    bool profiling = prof && *prof && *prof != '0';
    struct timespec t0, t1;
    if (profiling) {
        timespec_get(&t0, TIME_UTC);
        fprintf(stderr, "polajuice profile (%u threads)\n", pj_thread_count());
    }
    pj_raw_set_recover(options.recover);
    PjImage *source = linear_input ? pj_image_load_linear(input, &error)
                                   : load_input(input, &error);
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
    /* --lens auto: the file's lens, at its focal length and aperture,
     * sets the corrections the user left at zero; the recipe records the
     * numbers, so it replays the same on any input */
    if (lens_auto) {
        PjRawLens lens;
        float dist = 0.0f, fall = 0.0f;
        const char *name = pj_raw_last_lens(&lens) && pj_image_is_scene_linear(source)
                           ? pj_lens_profile(&lens, &dist, &fall) : NULL;
        if (name) {
            bool set_d = options.distortion == 0.0f, set_v = options.devignette == 0.0f;
            if (set_d) options.distortion = dist;
            if (set_v) options.devignette = fall;
            fprintf(stderr, "lens: %s at %gmm f/%g -> distortion %g%s, devignette %g%s\n",
                    name, (double)lens.focal, (double)lens.fnumber,
                    (double)dist, set_d ? "" : " (kept yours)", (double)fall, set_v ? "" : " (kept yours)");
        } else if (!pj_image_is_scene_linear(source))
            fprintf(stderr, "note: --lens auto needs a raw file (no lens data in %s)\n", input);
        else if (lens.have)
            fprintf(stderr, "note: no profile for this lens (%g-%gmm f/%g-%g%s%s); corrections left as given\n",
                    (double)lens.min_focal, (double)lens.max_focal, (double)lens.max_ap_min, (double)lens.max_ap_max,
                    lens.model[0] ? ", " : "", lens.model);
        else
            fprintf(stderr, "note: %s names no lens (a manual lens?); corrections left as given\n", input);
    }
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
    if (!strcmp(argv[1], "lut") || !strcmp(argv[1], "darktable") || !strcmp(argv[1], "curves")) {
        /* the look's flags start where apply's input would be */
        char **shifted = calloc((size_t)argc + 2, sizeof *shifted);
        if (!shifted) return EXIT_FAILURE;
        shifted[0] = argv[0]; shifted[1] = argv[1]; shifted[2] = "-";
        for (int i = 2; i < argc; ++i) shifted[i + 1] = argv[i];
        g_export = argv[1];
        int status = apply(argc + 1, shifted);
        free(shifted);
        return status;
    }
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
    if (!strcmp(argv[1], "paths")) {
        struct stat st;
        const char *dirs[3][2] = { { "films", film_library_root() }, { "prints", print_library_root() },
                                   { "web", web_root() } };
        for (int i = 0; i < 3; ++i)
            printf("%-7s %s%s\n", dirs[i][0], dirs[i][1],
                   stat(dirs[i][1], &st) == 0 ? "" : "  (missing: 'make install' or 'make fetch-luts')");
        return EXIT_SUCCESS;
    }
    usage(stderr);
    return EXIT_FAILURE;
}
