/*
 * A look, resolved: the camera, the film and print requests turned into
 * loaded LUTs, the film process and stem, the compatibility checks, and
 * the recipe text. Shared by the stills CLI and the server so that the
 * page, on the native engine, renders exactly what the command line
 * would.
 */
#define _POSIX_C_SOURCE 200809L

#include "look.h"
#include "filmlib.h"
#include "recipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(char *msg, size_t size, const char *format, ...)
{
    if (!msg || !size) return;
    va_list args;
    va_start(args, format);
    vsnprintf(msg, size, format, args);
    va_end(args);
}

bool look_prepare(Look *look, const char *tool, char *msg, size_t msgsize)
{
    look->color_lut = look->print_lut = NULL;
    look->recipe[0] = '\0';
    if (msg && msgsize) msg[0] = '\0';
    const char *camera = look->camera;
    if (!camera || !pj_preset_description(camera)) {
        say(msg, msgsize, "%s: unknown camera '%s'", tool, camera ? camera : "");
        return false;
    }
    PjRenderOptions *options = &look->options;

    /* Choose the film: explicit request > camera default > scalar engine. */
    char *film_path = NULL;
    if (look->film_request) {
        char processes[256];
        if (pj_preset_film_processes(camera, processes, sizeof processes) &&
            processes[0] == '\0' && !look->any_film) {
            say(msg, msgsize,
                "%s: %s is a sealed process and takes no film "
                "(the plate, cassette or dye-transfer process is the medium); "
                "use --any-film to override", tool, camera);
            return false;
        }
        film_path = resolve_film(look->film_request, look->quiet);
        if (!film_path) {
            say(msg, msgsize, "%s: film '%s' not found in the library", tool, look->film_request);
            return false;
        }
        const char *process = film_process_of(film_path);
        if (process && !look->any_film &&
            !pj_preset_accepts_film(camera, process)) {
            say(msg, msgsize,
                "%s: %s takes %s; '%s' is %s film.\n"
                "  (slide stock in negative chemistry is what "
                "--develop cross emulates; --any-film overrides)",
                tool, camera, processes[0] ? processes : "no film",
                look->film_request, process);
            free(film_path);
            return false;
        }
    } else if (!look->no_film) {
        const char *canonical = pj_preset_default_film(camera);
        if (canonical) {
            film_path = resolve_film(canonical, true);
            if (!film_path && !look->quiet)
                fprintf(stderr,
                        "note: canonical film '%s' not installed; using the "
                        "built-in scalar engine ('make fetch-luts' installs "
                        "the film library)\n",
                        canonical);
        }
    }

    PjError error = {{0}};
    options->color_lut = NULL;
    options->print_lut = NULL;
    options->film_stem = NULL;
    options->film_process = NULL;
    if (film_path) {
        options->film_process = film_process_of(film_path);
        film_stem_of_path(film_path, look->film_stem, sizeof look->film_stem);
        options->film_stem = look->film_stem;
        look->color_lut = pj_lut3d_load_cube(film_path, &error);
        free(film_path);
        if (!look->color_lut) { say(msg, msgsize, "%s: %s", tool, error.message); return false; }
        options->color_lut = look->color_lut;
    }
    look->print_stem[0] = '\0';
    if (look->print_request) {
        char *print_path = resolve_film(look->print_request, look->quiet);
        if (!print_path) {
            say(msg, msgsize, "%s: print stock '%s' not found", tool, look->print_request);
            look_release(look);
            return false;
        }
        film_stem_of_path(print_path, look->print_stem, sizeof look->print_stem);
        look->print_lut = pj_lut3d_load_cube(print_path, &error);
        free(print_path);
        if (!look->print_lut) { say(msg, msgsize, "%s: %s", tool, error.message); look_release(look); return false; }
        options->print_lut = look->print_lut;
    }
    if (!options->film_process)
        options->film_process = pj_preset_primary_process(camera);

    pj_recipe_format(camera, look->color_lut ? options->film_stem : "none",
                     look->print_stem[0] ? look->print_stem : NULL,
                     options, look->recipe, sizeof look->recipe);
    return true;
}

void look_release(Look *look)
{
    pj_lut3d_free(look->color_lut);
    pj_lut3d_free(look->print_lut);
    look->color_lut = look->print_lut = NULL;
    look->options.color_lut = look->options.print_lut = NULL;
}

PjImage *look_render(Look *look, const PjImage *source, size_t max_dim,
                     PjError *error)
{
    const PjImage *in = source;
    PjImage *small = NULL;
    if (max_dim) {
        /* downscale a copy: the caller keeps the source */
        small = pj_image_new(pj_image_width(source), pj_image_height(source), error);
        if (!small) return NULL;
        memcpy(pj_image_pixels(small), pj_image_pixels_const(source),
               pj_image_width(source) * pj_image_height(source) * 3 * sizeof(float));
        pj_image_copy_flags(small, source);
        small = pj_image_downscale(small, max_dim, error);
        if (!small) return NULL;
        in = small;
    }
    PjImage *result = pj_render(in, look->camera, &look->options, error);
    pj_image_free(small);
    if (!result) return NULL;
    pj_image_set_comment(result, look->recipe);
    return result;
}

bool look_from_params(Look *look, const LookParam *params, size_t count,
                      const char *tool, char *msg, size_t msgsize)
{
    memset(look, 0, sizeof *look);
    look->options.seed = UINT64_C(0x504f4c414a554943);
    look->options.strength = 1.0f;
    look->quiet = true;
    for (size_t i = 0; i < count; ++i) {
        const char *k = params[i].key, *v = params[i].value;
        char *end = NULL;
        if (!strcmp(k, "camera")) { snprintf(look->camera_buf, sizeof look->camera_buf, "%s", v); look->camera = look->camera_buf; }
        else if (!strcmp(k, "film")) {
            if (!strcmp(v, "none")) { look->no_film = true; look->film_request = NULL; }
            else if (!strcmp(v, "auto") || !*v) { look->no_film = false; look->film_request = NULL; }
            else { snprintf(look->film_buf, sizeof look->film_buf, "%s", v); look->film_request = look->film_buf; }
        }
        else if (!strcmp(k, "print")) {
            if (!strcmp(v, "none") || !*v) look->print_request = NULL;
            else { snprintf(look->print_buf, sizeof look->print_buf, "%s", v); look->print_request = look->print_buf; }
        }
        else if (!strcmp(k, "filter")) {
            if (!pj_contrast_filter_known(v)) { say(msg, msgsize, "%s: unknown filter '%s'", tool, v); return false; }
            snprintf(look->filter_buf, sizeof look->filter_buf, "%s", v);
            look->options.contrast_filter = strcmp(v, "none") && *v ? look->filter_buf : NULL;
        }
        else if (!strcmp(k, "develop")) {
            look->options.push = 0.0f; look->options.cross_process = false;
            if (!strcmp(v, "normal")) { }
            else if (!strcmp(v, "push+1")) look->options.push = 1.0f;
            else if (!strcmp(v, "push+2")) look->options.push = 2.0f;
            else if (!strcmp(v, "pull-1")) look->options.push = -1.0f;
            else if (!strcmp(v, "cross")) look->options.cross_process = true;
            else { say(msg, msgsize, "%s: unknown develop '%s'", tool, v); return false; }
        }
        else if (!strcmp(k, "grain")) {
            if (!pj_grain_model_known(v)) { say(msg, msgsize, "%s: unknown grain model '%s'", tool, v); return false; }
            snprintf(look->grain_buf, sizeof look->grain_buf, "%s", v);
            look->options.grain_model = look->grain_buf;
        }
        else if (!strcmp(k, "strength")) { look->options.strength = strtof(v, &end); if (!end || *end || look->options.strength < 0 || look->options.strength > 1) { say(msg, msgsize, "%s: strength out of range", tool); return false; } }
        else if (!strcmp(k, "age")) { look->options.age = strtof(v, &end); if (!end || *end || look->options.age < 0 || look->options.age > 1) { say(msg, msgsize, "%s: age out of range", tool); return false; } }
        else if (!strcmp(k, "edge")) { look->options.edge = strtof(v, &end); if (!end || *end || look->options.edge < 0 || look->options.edge > 2) { say(msg, msgsize, "%s: edge out of range", tool); return false; } }
        else if (!strcmp(k, "ev")) { look->options.ev = strtof(v, &end); if (!end || *end || look->options.ev < -4 || look->options.ev > 4) { say(msg, msgsize, "%s: ev out of range", tool); return false; } }
        else if (!strcmp(k, "halation")) { float h = strtof(v, &end); if (!end || *end || h < 0 || h > 2) { say(msg, msgsize, "%s: halation out of range", tool); return false; } look->options.halation = h == 0.0f ? PJ_HALATION_OFF : h; }
        else if (!strcmp(k, "seed")) { look->options.seed = strtoull(v, &end, 0); if (!end || *end) { say(msg, msgsize, "%s: bad seed", tool); return false; } }
        else if (!strcmp(k, "any_film")) look->any_film = !strcmp(v, "1") || !strcmp(v, "true");
        /* unknown keys (name, max, ext...) belong to the caller */
    }
    if (!look->camera) { say(msg, msgsize, "%s: no camera given", tool); return false; }
    return true;
}
