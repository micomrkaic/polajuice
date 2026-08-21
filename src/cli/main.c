/* The CLI uses POSIX interfaces (dirent, strdup, strcasecmp); ask for
 * their declarations explicitly since -std=c17 is strict ISO mode. */
#define _POSIX_C_SOURCE 200809L

#include "polajuice.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        "  polajuice inspect INPUT\n\n"
        "A look is a CAMERA (optics, grain, framing, process) plus a FILM\n"
        "(color chemistry as a .cube 3D LUT). Each camera has a canonical\n"
        "film used automatically when installed; -f overrides it.\n\n"
        "Options:\n"
        "  -c, --camera NAME       camera/format archetype (see 'cameras')\n"
        "  -f, --film NAME|FILE    film stock: a name searched in the film\n"
        "                          library, or a path to a .cube file\n"
        "      --no-film           force the built-in scalar color engine\n"
        "      --any-film          bypass camera/film compatibility checks\n"
        "  -o, --output PATH       output path; default INPUT_CAMERA.ext\n"
        "                          formats by extension: .ppm .png .jpg .jpeg\n"
        "      --strength NUMBER   blend strength from 0 to 1 (default 1)\n"
        "      --develop MODE      development: normal (default), push+1,\n"
        "                          push+2, pull-1, or cross (E-6 in C-41);\n"
        "                          not applicable to instant cameras\n"
        "      --age NUMBER        storage aging from 0 (fresh) to 1\n"
        "                          (fog, faded contrast, magenta drift)\n"
        "      --seed INTEGER      deterministic grain seed\n"
        "  -h, --help              show this help\n\n"
        "Film library: $POLAJUICE_FILMS, else ./data/luts ('make fetch-luts').\n",
        pj_version_string());
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
    PjImage *image = pj_image_load(path, &error);
    if (!image) return fail(&error);
    printf("path: %s\nwidth: %zu\nheight: %zu\ncolorspace: sRGB\n",
           path, pj_image_width(image), pj_image_height(image));
    pj_image_free(image);
    return EXIT_SUCCESS;
}

/* ---- film library ----------------------------------------------------- */

static const char *film_library_root(void)
{
    const char *env = getenv("POLAJUICE_FILMS");
    return env && *env ? env : "data/luts";
}

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
} FilmList;

static void film_list_add(FilmList *list, const char *path)
{
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2 : 32;
        char **grown = realloc(list->paths, next * sizeof *grown);
        if (!grown) return;
        list->paths = grown;
        list->capacity = next;
    }
    char *copy = strdup(path);
    if (copy) list->paths[list->count++] = copy;
}

static void film_list_free(FilmList *list)
{
    for (size_t i = 0; i < list->count; ++i) free(list->paths[i]);
    free(list->paths);
}

static void collect_cubes(const char *dir, FilmList *list, int depth)
{
    if (depth > 6) return;
    DIR *handle = opendir(dir);
    if (!handle) return;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[4096];
        int n = snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof path) continue;
        struct stat info;
        if (stat(path, &info) != 0) continue;
        if (S_ISDIR(info.st_mode))
            collect_cubes(path, list, depth + 1);
        else {
            size_t len = strlen(entry->d_name);
            if (len > 5 && !strcasecmp(entry->d_name + len - 5, ".cube"))
                film_list_add(list, path);
        }
    }
    closedir(handle);
}

static const char *film_stem(const char *path, char *buffer, size_t size)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    size_t len = strlen(base);
    if (len > 5) len -= 5;   /* drop ".cube" */
    if (len >= size) len = size - 1;
    memcpy(buffer, base, len);
    buffer[len] = '\0';
    return buffer;
}

static bool contains_nocase(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; ++p)
        if (!strncasecmp(p, needle, nlen)) return true;
    return false;
}

/*
 * Infer a film's process from its resolved library path (family directory)
 * with stem-based overrides for instant stocks that live in other packs.
 * Returns a token for pj_preset_accepts_film, or NULL when unknowable
 * (e.g. a user-supplied cube outside the library), which is never blocked.
 */
static const char *film_process_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *stem = slash ? slash + 1 : path;
    if (!strncmp(stem, "polaroid_px", 11) ||
        !strncmp(stem, "polaroid_time_zero", 18))
        return "integral";
    if (!strncmp(stem, "polaroid_66", 11) || !strncmp(stem, "polaroid_69", 11) ||
        !strncmp(stem, "fuji_fp", 7))
        return "pack";
    if (strstr(path, "instant_consumer")) return "integral";
    if (strstr(path, "instant_pro")) return "pack";
    if (strstr(path, "colorslide")) return "slide";
    if (strstr(path, "/bw/")) return "bw";
    if (strstr(path, "negative_old") || strstr(path, "negative_new"))
        return "negative";
    return NULL;
}

/*
 * Resolve a film argument to a .cube path.  A string that names an existing
 * file (or contains a slash) is used verbatim; anything else is matched
 * against the film library: exact stem match first, then case-insensitive
 * substring.  Returns a malloc'd path, or NULL with a message on stderr
 * (unless quiet, used for camera defaults which fall back to scalars).
 */
static char *resolve_film(const char *request, bool quiet)
{
    struct stat info;
    if (stat(request, &info) == 0 && !S_ISDIR(info.st_mode))
        return strdup(request);
    if (strchr(request, '/')) {
        if (!quiet)
            fprintf(stderr, "polajuice: film file not found: %s\n", request);
        return NULL;
    }

    FilmList library = {0};
    collect_cubes(film_library_root(), &library, 0);
    if (library.count == 0) {
        if (!quiet)
            fprintf(stderr,
                    "polajuice: no film library at '%s' "
                    "(run 'make fetch-luts' or set POLAJUICE_FILMS)\n",
                    film_library_root());
        film_list_free(&library);
        return NULL;
    }

    char stem[512];
    char *exact = NULL;
    FilmList matches = {0};
    for (size_t i = 0; i < library.count; ++i) {
        film_stem(library.paths[i], stem, sizeof stem);
        if (!strcasecmp(stem, request)) {
            exact = strdup(library.paths[i]);
            break;
        }
        if (contains_nocase(stem, request))
            film_list_add(&matches, library.paths[i]);
    }

    char *result = NULL;
    if (exact)
        result = exact;
    else if (matches.count == 1)
        result = strdup(matches.paths[0]);
    else if (!quiet && matches.count == 0) {
        fprintf(stderr, "polajuice: no film matches '%s' in %s\n",
                request, film_library_root());
    } else if (!quiet) {
        fprintf(stderr, "polajuice: '%s' is ambiguous (%zu films):\n",
                request, matches.count);
        for (size_t i = 0; i < matches.count && i < 12; ++i) {
            film_stem(matches.paths[i], stem, sizeof stem);
            fprintf(stderr, "  %s\n", stem);
        }
        if (matches.count > 12) fprintf(stderr, "  ...\n");
    }
    film_list_free(&matches);
    film_list_free(&library);
    return result;
}

static int list_films(const char *filter)
{
    FilmList library = {0};
    collect_cubes(film_library_root(), &library, 0);
    if (library.count == 0) {
        fprintf(stderr,
                "no film library at '%s' "
                "(run 'make fetch-luts' or set POLAJUICE_FILMS)\n",
                film_library_root());
        return EXIT_FAILURE;
    }
    char stem[512];
    size_t shown = 0;
    for (size_t i = 0; i < library.count; ++i) {
        film_stem(library.paths[i], stem, sizeof stem);
        if (filter && !contains_nocase(stem, filter)) continue;
        printf("%s\n", stem);
        ++shown;
    }
    film_list_free(&library);
    if (filter && shown == 0)
        printf("(no films match '%s')\n", filter);
    return EXIT_SUCCESS;
}

/* ---- apply ------------------------------------------------------------ */

static int apply(int argc, char **argv)
{
    const char *input = argc > 2 ? argv[2] : NULL;
    const char *output = NULL;
    const char *camera = NULL;
    const char *film_request = NULL;
    bool no_film = false;
    bool any_film = false;
    PjRenderOptions options = {.seed = UINT64_C(0x504f4c414a554943),
                               .strength = 1.0f};

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
        else if (!strcmp(argv[i], "--develop") && i + 1 < argc) {
            const char *dev = argv[++i];
            if (!strcmp(dev, "normal")) { /* defaults */ }
            else if (!strcmp(dev, "push+1")) options.push = 1.0f;
            else if (!strcmp(dev, "push+2")) options.push = 2.0f;
            else if (!strcmp(dev, "pull-1")) options.push = -1.0f;
            else if (!strcmp(dev, "cross")) options.cross_process = true;
            else {
                fprintf(stderr, "invalid develop mode '%s' "
                        "(normal, push+1, push+2, pull-1, cross)\n", dev);
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
        fprintf(stderr, "--film and --no-film are mutually exclusive\n");
        return EXIT_FAILURE;
    }
    char derived[4096];
    if (!output) {
        const char *dot = strrchr(input, '.');
        const char *slash = strrchr(input, '/');
        if (!dot || (slash && dot < slash)) dot = input + strlen(input);
        int n = snprintf(derived, sizeof derived, "%.*s_%s%s",
                         (int)(dot - input), input, camera, dot);
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

    /* Choose the film: explicit request > camera default > scalar engine. */
    char *film_path = NULL;
    if (film_request) {
        char processes[256];
        if (pj_preset_film_processes(camera, processes, sizeof processes) &&
            processes[0] == '\0' && !any_film) {
            fprintf(stderr,
                    "polajuice: %s is a sealed process and takes no film "
                    "(the plate or dye-transfer process is the medium); "
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
        lut = pj_lut3d_load_cube(film_path, &error);
        free(film_path);
        if (!lut) return fail(&error);
        options.color_lut = lut;
    }
    PjImage *source = pj_image_load(input, &error);
    if (!source) {
        pj_lut3d_free(lut);
        return fail(&error);
    }
    PjImage *result = pj_render(source, camera, &options, &error);
    pj_image_free(source);
    pj_lut3d_free(lut);
    if (!result) return fail(&error);
    bool saved = pj_image_save(result, output, &error);
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
    if (!strcmp(argv[1], "inspect") && argc > 2)
        return inspect_image(argv[2]);
    usage(stderr);
    return EXIT_FAILURE;
}
