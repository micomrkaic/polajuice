/* Shared film-library helpers for the stills and movie front-ends.
 * POSIX (dirent); linked into the CLIs only, never into the wasm build. */
#define _POSIX_C_SOURCE 200809L

#include "polajuice.h"
#include "filmlib.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* ---- where the assets are --------------------------------------------
 * The binaries run from the source tree or from an install, and a user
 * may keep a library elsewhere, so every asset directory (the film
 * library, the print stocks, the web page) is found in this order:
 *   1. its own variable ($POLAJUICE_FILMS, $POLAJUICE_PRINTS, $POLAJUICE_WEB)
 *   2. $POLAJUICE_HOME/<name>, one variable for a whole relocated set
 *   3. the source tree's path relative to the working directory, when it
 *      exists (data/luts, data/prints, web - running from the repo)
 *   4. ${XDG_DATA_HOME:-~/.local/share}/polajuice/<name> ('make install')
 *   5. POLAJUICE_DATADIR/<name>, baked in at build time from the
 *      Makefile's datadir (a system-wide 'make install PREFIX=/usr/local')
 * The first directory that exists wins; if none does, 4 is returned so
 * the error message names the place 'make install' would fill. */
#ifndef POLAJUICE_DATADIR
#define POLAJUICE_DATADIR "/usr/local/share/polajuice"
#endif

static bool dir_exists(const char *path)
{
    struct stat st;
    return path && *path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

const char *asset_dir(const char *env, const char *name, const char *in_tree, char *buf, size_t n)
{
    const char *own = env ? getenv(env) : NULL;
    if (own && *own) { snprintf(buf, n, "%s", own); return buf; }
    const char *home_set = getenv("POLAJUICE_HOME");
    if (home_set && *home_set) { snprintf(buf, n, "%s/%s", home_set, name); return buf; }
    if (dir_exists(in_tree)) { snprintf(buf, n, "%s", in_tree); return buf; }
    char user[1024];
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(user, sizeof user, "%s/polajuice/%s", xdg, name);
    else if (home && *home) snprintf(user, sizeof user, "%s/.local/share/polajuice/%s", home, name);
    else user[0] = '\0';
    if (dir_exists(user)) { snprintf(buf, n, "%s", user); return buf; }
    snprintf(buf, n, "%s/%s", POLAJUICE_DATADIR, name);
    if (dir_exists(buf)) return buf;
    snprintf(buf, n, "%s", user[0] ? user : in_tree);
    return buf;
}

/* ---- film library ----------------------------------------------------- */

const char *film_library_root(void)
{
    static char buf[2048];
    return asset_dir("POLAJUICE_FILMS", "luts", "data/luts", buf, sizeof buf);
}

/* Print stocks live beside the fetched film library: shipped synthetic
 * placeholders (and any measured print cubes the user adds) in
 * data/prints, overridable with POLAJUICE_PRINTS. */
const char *print_library_root(void)
{
    static char buf[2048];
    return asset_dir("POLAJUICE_PRINTS", "prints", "data/prints", buf, sizeof buf);
}

const char *web_root(void)
{
    static char buf[2048];
    return asset_dir("POLAJUICE_WEB", "web", "web", buf, sizeof buf);
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
const char *film_process_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *stem = slash ? slash + 1 : path;
    if (!strncmp(stem, "polaroid_px", 11) ||
        !strncmp(stem, "polaroid_time_zero", 18))
        return "integral";
    if (!strncmp(stem, "polaroid_66", 11) || !strncmp(stem, "polaroid_69", 11) ||
        !strncmp(stem, "fuji_fp", 7))
        return "pack";
    if (strstr(path, "/prints/") || strstr(path, "prints/synthetic_") ||
        !strncmp(stem, "synthetic_", 10))
        return "print";
    if (!strncmp(stem, "fuji_neopan", 11) || !strncmp(stem, "fuji_acros", 10))
        return "bw";   /* B+W stocks shelved in the color-negative pack */
    /* Family directories are matched below the library root only. The
     * root itself can contain anything: macOS puts per-user temp trees
     * under /var/folders/XX/ with a random two-letter XX, and a user
     * whose XX was "bw" had every stock in a temp library classified as
     * black-and-white, Portra included. */
    const char *rel = path;
    const char *root = film_library_root();
    size_t rlen = strlen(root);
    while (rlen && root[rlen - 1] == '/') --rlen;
    if (rlen && !strncmp(path, root, rlen) && path[rlen] == '/')
        rel = path + rlen + 1;
    if (strstr(rel, "instant_consumer")) return "integral";
    if (strstr(rel, "instant_pro")) return "pack";
    if (strstr(rel, "colorslide")) return "slide";
    if (!strncmp(rel, "bw/", 3) || strstr(rel, "/bw/")) return "bw";
    if (strstr(rel, "negative_old") || strstr(rel, "negative_new"))
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
char *resolve_film(const char *request, bool quiet)
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
    collect_cubes(print_library_root(), &library, 0);
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

    /* Tie-break before declaring ambiguity: if exactly one candidate
     * ends with the request at an underscore boundary, the user typed a
     * complete stock name that other stems merely contain (the
     * kodachrome_64 vs kodachrome_64_generic case). */
    char *suffix_hit = NULL;
    if (!exact && matches.count > 1) {
        size_t req_len = strlen(request);
        size_t hits = 0;
        for (size_t i = 0; i < matches.count; ++i) {
            film_stem(matches.paths[i], stem, sizeof stem);
            size_t stem_len = strlen(stem);
            if (stem_len >= req_len &&
                !strcasecmp(stem + stem_len - req_len, request) &&
                (stem_len == req_len || stem[stem_len - req_len - 1] == '_')) {
                suffix_hit = matches.paths[i];
                ++hits;
            }
        }
        if (hits != 1) suffix_hit = NULL;
    }

    char *result = NULL;
    if (exact)
        result = exact;
    else if (suffix_hit)
        result = strdup(suffix_hit);
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

int list_films(const char *filter)
{
    FilmList library = {0};
    collect_cubes(film_library_root(), &library, 0);
    collect_cubes(print_library_root(), &library, 0);
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

