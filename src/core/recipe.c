/*
 * The recipe: a whole look as key=value text. One line per parameter,
 * '#' comments, blank lines ignored; the format is docs/RECIPES.md. The
 * engine writes it (here) and embeds it in every JPEG it saves; the
 * CLI reads it (src/cli/recipe.c) and the page reads and writes it in
 * JavaScript. key=value sources into a shell as well, which is the
 * point of not using JSON.
 */
#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* a float with the fewest digits that round-trips the values the
 * options take (sliders step by 0.05 and 0.1) */
static void put_num(char *out, size_t size, float v)
{
    if (fabsf(v - roundf(v)) < 1e-6f) snprintf(out, size, "%.0f", (double)v);
    else snprintf(out, size, "%.4g", (double)v);
}

size_t pj_recipe_format(const char *camera, const char *film,
                        const char *print, const PjRenderOptions *o,
                        char *buf, size_t size)
{
    const char *develop = "normal";
    if (o->cross_process) develop = "cross";
    else if (o->push >= 2.0f) develop = "push+2";
    else if (o->push >= 1.0f) develop = "push+1";
    else if (o->push <= -1.0f) develop = "pull-1";
    float halation = o->halation == 0.0f ? 1.0f : (o->halation < 0.0f ? 0.0f : o->halation);
    char strength[32], age[32], edge[32], ev[32], hal[32];
    put_num(strength, sizeof strength, o->strength);
    put_num(age, sizeof age, o->age);
    put_num(edge, sizeof edge, o->edge);
    put_num(ev, sizeof ev, o->ev);
    put_num(hal, sizeof hal, halation);
    int n = snprintf(buf, size,
                     "# polajuice recipe 1\n"
                     "camera=%s\n"
                     "film=%s\n"
                     "print=%s\n"
                     "filter=%s\n"
                     "strength=%s\n"
                     "develop=%s\n"
                     "age=%s\n"
                     "grain=%s\n"
                     "edge=%s\n"
                     "ev=%s\n"
                     "halation=%s\n"
                     "seed=%llu\n",
                     camera ? camera : "",
                     film && *film ? film : "none",
                     print && *print ? print : "none",
                     o->contrast_filter && *o->contrast_filter &&
                         strcmp(o->contrast_filter, "none") ? o->contrast_filter : "none",
                     strength, develop, age,
                     o->grain_model && *o->grain_model ? o->grain_model : "classic",
                     edge, ev, hal,
                     (unsigned long long)o->seed);
    return n < 0 ? 0 : (size_t)n;
}

bool pj_recipe_is_recipe(const char *text)
{
    return text && !strncmp(text, "# polajuice recipe", 18);
}
