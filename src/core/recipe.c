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

static const PjSceneKey scene_keys[] = {
    {"temp", -2.0f, 2.0f}, {"tint", -1.0f, 1.0f}, {"black", -0.1f, 0.1f},
    {"shadows", -2.0f, 2.0f}, {"highlights", -1.0f, 1.0f}, {"contrast", -1.0f, 1.0f},
    {"grad", 0.0f, 3.0f}, {"gradangle", 0.0f, 360.0f}, {"gradpos", -0.5f, 0.5f},
    {"denoise", 0.0f, 3.0f}, {"distortion", -0.3f, 0.3f}, {"devignette", 0.0f, 2.0f},
    {"rotate", -15.0f, 15.0f},
    {"trimx", 0.0f, 1.0f}, {"trimy", 0.0f, 1.0f}, {"trimw", 0.0f, 1.0f}, {"trimh", 0.0f, 1.0f},
};

const PjSceneKey *pj_scene_keys(size_t *count)
{
    if (count) *count = sizeof scene_keys / sizeof scene_keys[0];
    return scene_keys;
}

static float *scene_slot(PjRenderOptions *o, const char *key)
{
    if (!strcmp(key, "temp")) return &o->temp;
    if (!strcmp(key, "tint")) return &o->tint;
    if (!strcmp(key, "black")) return &o->black;
    if (!strcmp(key, "shadows")) return &o->shadows;
    if (!strcmp(key, "highlights")) return &o->highlights;
    if (!strcmp(key, "contrast")) return &o->contrast;
    if (!strcmp(key, "grad")) return &o->grad;
    if (!strcmp(key, "gradangle")) return &o->grad_angle;
    if (!strcmp(key, "gradpos")) return &o->grad_pos;
    if (!strcmp(key, "denoise")) return &o->denoise;
    if (!strcmp(key, "distortion")) return &o->distortion;
    if (!strcmp(key, "devignette")) return &o->devignette;
    if (!strcmp(key, "rotate")) return &o->rotate;
    if (!strcmp(key, "trimx")) return &o->trim_x;
    if (!strcmp(key, "trimy")) return &o->trim_y;
    if (!strcmp(key, "trimw")) return &o->trim_w;
    if (!strcmp(key, "trimh")) return &o->trim_h;
    return NULL;
}

bool pj_scene_is_key(const char *key)
{
    for (size_t i = 0; i < sizeof scene_keys / sizeof scene_keys[0]; ++i)
        if (!strcmp(scene_keys[i].key, key)) return true;
    return false;
}

bool pj_scene_set(PjRenderOptions *o, const char *key, const char *value)
{
    for (size_t i = 0; i < sizeof scene_keys / sizeof scene_keys[0]; ++i) {
        if (strcmp(scene_keys[i].key, key)) continue;
        char *end = NULL;
        float v = strtof(value, &end);
        if (!end || end == value || *end || v < scene_keys[i].lo || v > scene_keys[i].hi) return false;
        *scene_slot(o, key) = v;
        return true;
    }
    return false;
}

float pj_scene_get(const PjRenderOptions *o, const char *key)
{
    float *slot = scene_slot((PjRenderOptions *)o, key);
    return slot ? *slot : 0.0f;
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
    if (n < 0) return 0;
    size_t used = (size_t)n;
    /* the scene, lens and framing keys, every one, in table order */
    size_t count = 0;
    const PjSceneKey *keys = pj_scene_keys(&count);
    for (size_t i = 0; i < count && used < size; ++i) {
        char num[32];
        put_num(num, sizeof num, pj_scene_get(o, keys[i].key));
        int m = snprintf(buf + used, size - used, "%s=%s\n", keys[i].key, num);
        if (m < 0) break;
        used += (size_t)m;
    }
    return used;
}

bool pj_recipe_is_recipe(const char *text)
{
    return text && !strncmp(text, "# polajuice recipe", 18);
}
