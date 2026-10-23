/*
 * Reading recipes for the command-line front-ends: a key=value file, or
 * a JPEG that carries one in its comment (every JPEG polajuice saves
 * does). The values land in the same variables the option parser sets,
 * so flags given after --recipe override the recipe line by line.
 */
#define _POSIX_C_SOURCE 200809L

#include "recipe.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static bool parse_float(const char *v, float lo, float hi, float *out, const char *key,
                        const char *tool)
{
    char *end = NULL;
    float x = strtof(v, &end);
    if (!end || *end || x < lo || x > hi) {
        fprintf(stderr, "%s: recipe: %s=%s is out of range (%g..%g)\n", tool, key, v,
                (double)lo, (double)hi);
        return false;
    }
    *out = x;
    return true;
}

static char *read_text(const char *path)
{
    /* a JPEG? take its comment */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char head[2] = {0, 0};
    size_t got = fread(head, 1, 2, f);
    fclose(f);
    if (got == 2 && head[0] == 0xFF && head[1] == 0xD8) return pj_jpeg_read_comment(path);
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || n > 65536) { fclose(f); return NULL; }
    rewind(f);
    char *text = malloc((size_t)n + 1);
    if (!text) { fclose(f); return NULL; }
    if (fread(text, 1, (size_t)n, f) != (size_t)n) { free(text); fclose(f); return NULL; }
    fclose(f);
    text[n] = '\0';
    return text;
}

bool recipe_load(const char *path, Recipe *r, const char *tool)
{
    memset(r, 0, sizeof *r);
    char *text = read_text(path);
    if (!text) {
        fprintf(stderr, "%s: cannot read a recipe from '%s'\n", tool, path);
        return false;
    }
    if (!pj_recipe_is_recipe(text)) {
        /* a JPEG without one, or a text file that does not announce
         * itself: accept the text if it looks like key=value lines */
        if (!strchr(text, '=')) {
            fprintf(stderr, "%s: '%s' holds no recipe\n", tool, path);
            free(text);
            return false;
        }
    }
    bool ok = true;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) { fprintf(stderr, "%s: recipe: ignoring line '%s'\n", tool, s); continue; }
        *eq = '\0';
        char *key = trim(s), *val = trim(eq + 1);
        if (!strcmp(key, "camera")) { snprintf(r->camera, sizeof r->camera, "%s", val); r->has_camera = *val != 0; }
        else if (!strcmp(key, "film")) { snprintf(r->film, sizeof r->film, "%s", val); r->has_film = *val != 0; }
        else if (!strcmp(key, "print")) { snprintf(r->print, sizeof r->print, "%s", val); r->has_print = *val != 0; }
        else if (!strcmp(key, "filter")) { snprintf(r->filter, sizeof r->filter, "%s", val); r->has_filter = *val != 0; }
        else if (!strcmp(key, "develop")) { snprintf(r->develop, sizeof r->develop, "%s", val); r->has_develop = *val != 0; }
        else if (!strcmp(key, "grain")) { snprintf(r->grain, sizeof r->grain, "%s", val); r->has_grain = *val != 0; }
        else if (!strcmp(key, "strength")) { ok = parse_float(val, 0, 1, &r->strength, key, tool) && ok; r->has_strength = true; }
        else if (!strcmp(key, "age")) { ok = parse_float(val, 0, 1, &r->age, key, tool) && ok; r->has_age = true; }
        else if (!strcmp(key, "edge")) { ok = parse_float(val, 0, 2, &r->edge, key, tool) && ok; r->has_edge = true; }
        else if (!strcmp(key, "ev")) { ok = parse_float(val, -4, 4, &r->ev, key, tool) && ok; r->has_ev = true; }
        else if (!strcmp(key, "halation")) { ok = parse_float(val, 0, 2, &r->halation, key, tool) && ok; r->has_halation = true; }
        else if (!strcmp(key, "seed")) {
            char *end = NULL;
            r->seed = strtoull(val, &end, 0);
            if (!end || *end) { fprintf(stderr, "%s: recipe: bad seed '%s'\n", tool, val); ok = false; }
            else r->has_seed = true;
        }
        else fprintf(stderr, "%s: recipe: unknown key '%s' ignored\n", tool, key);
    }
    free(text);
    return ok;
}

bool recipe_apply(const Recipe *r, PjRenderOptions *o, const char **camera,
                  const char **film_request, bool *no_film,
                  const char **print_request, const char *tool)
{
    if (r->has_camera) *camera = r->camera;
    if (r->has_film) {
        if (!strcmp(r->film, "none")) { *no_film = true; *film_request = NULL; }
        else if (!strcmp(r->film, "auto")) { *no_film = false; *film_request = NULL; }
        else { *no_film = false; *film_request = r->film; }
    }
    if (r->has_print) *print_request = strcmp(r->print, "none") ? r->print : NULL;
    if (r->has_filter) {
        if (!pj_contrast_filter_known(r->filter)) {
            fprintf(stderr, "%s: recipe: unknown filter '%s'\n", tool, r->filter);
            return false;
        }
        o->contrast_filter = strcmp(r->filter, "none") ? r->filter : NULL;
    }
    if (r->has_develop) {
        o->push = 0.0f; o->cross_process = false;
        if (!strcmp(r->develop, "normal")) { }
        else if (!strcmp(r->develop, "push+1")) o->push = 1.0f;
        else if (!strcmp(r->develop, "push+2")) o->push = 2.0f;
        else if (!strcmp(r->develop, "pull-1")) o->push = -1.0f;
        else if (!strcmp(r->develop, "cross")) o->cross_process = true;
        else { fprintf(stderr, "%s: recipe: unknown develop '%s'\n", tool, r->develop); return false; }
    }
    if (r->has_grain) {
        if (!pj_grain_model_known(r->grain)) {
            fprintf(stderr, "%s: recipe: unknown grain model '%s'\n", tool, r->grain);
            return false;
        }
        o->grain_model = r->grain;
    }
    if (r->has_strength) o->strength = r->strength;
    if (r->has_age) o->age = r->age;
    if (r->has_edge) o->edge = r->edge;
    if (r->has_ev) o->ev = r->ev;
    if (r->has_halation) o->halation = r->halation == 0.0f ? PJ_HALATION_OFF : r->halation;
    if (r->has_seed) o->seed = r->seed;
    return true;
}

const char *film_stem_of_path(const char *path, char *buf, size_t size)
{
    if (!path) return NULL;
    const char *slash = strrchr(path, '/');
    snprintf(buf, size, "%s", slash ? slash + 1 : path);
    char *dot = strrchr(buf, '.');
    if (dot) *dot = '\0';
    return buf;
}
