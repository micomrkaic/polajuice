#ifndef POLAJUICE_CLI_RECIPE_H
#define POLAJUICE_CLI_RECIPE_H

#include "polajuice.h"

/* A recipe as read from a file or a JPEG comment: every field is
 * optional, so a partial recipe can set a few keys and leave the rest
 * to the defaults or to flags. */
typedef struct {
    char camera[64], film[128], print[128], filter[32], develop[16], grain[16];
    float strength, age, edge, ev, halation;
    unsigned long long seed;
    bool has_camera, has_film, has_print, has_filter, has_develop, has_grain;
    bool has_strength, has_age, has_edge, has_ev, has_halation, has_seed;
} Recipe;

bool recipe_load(const char *path, Recipe *r, const char *tool);
/* The recipe's strings stay referenced from r: keep it alive while the
 * options are in use. */
bool recipe_apply(const Recipe *r, PjRenderOptions *o, const char **camera,
                  const char **film_request, bool *no_film,
                  const char **print_request, const char *tool);
const char *film_stem_of_path(const char *path, char *buf, size_t size);

#endif
