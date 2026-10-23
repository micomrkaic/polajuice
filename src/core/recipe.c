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
#include <stdlib.h>
#include <string.h>

/* a float with the fewest digits that round-trips the values the
 * options take (sliders step by 0.05 and 0.1) */
static void put_num(char *out, size_t size, float v)
{
    if (fabsf(v - roundf(v)) < 1e-6f) snprintf(out, size, "%.0f", (double)v);
    else snprintf(out, size, "%.4g", (double)v);
}

int pj_crop_parse(const char *name)
{
    if (!name || !*name || !strcmp(name, "camera")) return PJ_CROP_CAMERA;
    if (!strcmp(name, "full")) return PJ_CROP_FULL;
    return -1;
}

const char *pj_crop_name(int crop)
{
    return crop == PJ_CROP_FULL ? "full" : "camera";
}

bool pj_develop_parse(const char *text, float *push, bool *cross)
{
    if (!text) return false;
    const char *p = text;
    bool x = false;
    int sign = 0;
    if (!strncmp(p, "normal", 6)) { p += 6; }
    else if (!strncmp(p, "cross", 5)) { p += 5; x = true; }
    else if (!strncmp(p, "push", 4)) { p += 4; sign = 1; }
    else if (!strncmp(p, "pull", 4)) { p += 4; sign = -1; }
    else return false;
    float stops = 0.0f;
    if (*p) {
        /* a signed number: push+1, pull-1, cross+1.5; push and pull also
         * take the bare magnitude (push1 == push+1, pull1 == pull-1) */
        if (sign == 0 && *p != '+' && *p != '-') return false;
        char *end = NULL;
        float v = strtof(p, &end);
        if (!end || end == p || *end) return false;
        if (sign > 0 && *p == '-') return false;    /* push-1 is a contradiction */
        if (sign < 0) {
            if (*p == '+') return false;
            if (*p != '-') v = -v;                   /* pull1 means a stop down */
        }
        stops = v;
        if (stops < PJ_PUSH_MIN || stops > PJ_PUSH_MAX) return false;
    } else if (sign != 0) return false;        /* "push" alone says nothing */
    *push = stops;
    *cross = x;
    return true;
}

void pj_develop_format(float push, bool cross, char *buf, size_t size)
{
    if (push == 0.0f) { snprintf(buf, size, "%s", cross ? "cross" : "normal"); return; }
    char num[32];
    if (fabsf(push - roundf(push)) < 1e-6f) snprintf(num, sizeof num, "%.0f", (double)fabsf(push));
    else snprintf(num, sizeof num, "%.4g", (double)fabsf(push));
    snprintf(buf, size, "%s%c%s", cross ? "cross" : push > 0.0f ? "push" : "pull",
             push > 0.0f ? '+' : '-', num);
}

size_t pj_recipe_format(const char *camera, const char *film,
                        const char *print, const PjRenderOptions *o,
                        char *buf, size_t size)
{
    char develop[48];
    pj_develop_format(o->push, o->cross_process, develop, sizeof develop);
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
                     "seed=%llu\n"
                     "crop=%s\n",
                     camera ? camera : "",
                     film && *film ? film : "none",
                     print && *print ? print : "none",
                     o->contrast_filter && *o->contrast_filter &&
                         strcmp(o->contrast_filter, "none") ? o->contrast_filter : "none",
                     strength, develop, age,
                     o->grain_model && *o->grain_model ? o->grain_model : "classic",
                     edge, ev, hal,
                     (unsigned long long)o->seed,
                     pj_crop_name(o->crop));
    return n < 0 ? 0 : (size_t)n;
}

bool pj_recipe_is_recipe(const char *text)
{
    return text && !strncmp(text, "# polajuice recipe", 18);
}
