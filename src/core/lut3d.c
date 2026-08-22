#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *skip_space(char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

static bool parse_triplet(const char *text, float out[3])
{
    char *end = NULL;
    for (size_t i = 0; i < 3; ++i) {
        errno = 0;
        out[i] = strtof(text, &end);
        if (errno || end == text || !isfinite(out[i])) return false;
        text = end;
    }
    text = skip_space((char *)text);
    return *text == '\0' || *text == '#';
}

PjLut3D *pj_lut3d_load_cube(const char *path, PjError *error)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        pj_set_error(error, "cannot open LUT '%s': %s", path, strerror(errno));
        return NULL;
    }

    PjLut3D *lut = calloc(1, sizeof *lut);
    if (!lut) {
        fclose(file);
        pj_set_error(error, "could not allocate LUT descriptor");
        return NULL;
    }
    for (size_t c = 0; c < 3; ++c) lut->domain_max[c] = 1.0f;

    char line[1024];
    size_t count = 0;
    while (fgets(line, sizeof line, file)) {
        char *s = skip_space(line);
        char *newline = strpbrk(s, "\r\n");
        if (newline) *newline = '\0';
        if (!*s || *s == '#') continue;
        if (!strncmp(s, "TITLE", 5)) continue;
        if (!strncmp(s, "LUT_1D_SIZE", 11)) {
            pj_set_error(error, "'%s' contains a 1D LUT; a 3D LUT is required", path);
            goto fail;
        }
        if (!strncmp(s, "LUT_3D_SIZE", 11)) {
            char *value = skip_space(s + 11);
            char *end = NULL;
            errno = 0;
            unsigned long parsed = strtoul(value, &end, 10);
            end = skip_space(end);
            if (errno || *end || parsed < 2 || parsed > 128 || lut->rgb) {
                pj_set_error(error, "invalid LUT_3D_SIZE in '%s'", path);
                goto fail;
            }
            lut->size = (size_t)parsed;
            size_t entries = lut->size * lut->size * lut->size;
            lut->rgb = malloc(entries * 3 * sizeof *lut->rgb);
            if (!lut->rgb) {
                pj_set_error(error, "could not allocate %lux%lux%lu LUT",
                             parsed, parsed, parsed);
                goto fail;
            }
            continue;
        }
        if (!strncmp(s, "DOMAIN_MIN", 10)) {
            if (!parse_triplet(skip_space(s + 10), lut->domain_min)) {
                pj_set_error(error, "invalid DOMAIN_MIN in '%s'", path);
                goto fail;
            }
            continue;
        }
        if (!strncmp(s, "DOMAIN_MAX", 10)) {
            if (!parse_triplet(skip_space(s + 10), lut->domain_max)) {
                pj_set_error(error, "invalid DOMAIN_MAX in '%s'", path);
                goto fail;
            }
            continue;
        }

        if (!lut->rgb) {
            pj_set_error(error, "LUT data precedes LUT_3D_SIZE in '%s'", path);
            goto fail;
        }
        size_t entries = lut->size * lut->size * lut->size;
        if (count >= entries || !parse_triplet(s, &lut->rgb[count * 3])) {
            pj_set_error(error, "invalid or excessive LUT data in '%s'", path);
            goto fail;
        }
        ++count;
    }
    if (ferror(file)) {
        pj_set_error(error, "error while reading LUT '%s'", path);
        goto fail;
    }
    fclose(file);

    size_t expected = lut->size * lut->size * lut->size;
    if (!lut->rgb || count != expected) {
        pj_set_error(error, "LUT '%s' has %zu entries; expected %zu",
                     path, count, expected);
        pj_lut3d_free(lut);
        return NULL;
    }
    for (size_t c = 0; c < 3; ++c) {
        if (!(lut->domain_max[c] > lut->domain_min[c])) {
            pj_set_error(error, "invalid LUT domain in '%s'", path);
            pj_lut3d_free(lut);
            return NULL;
        }
    }
    return lut;

fail:
    fclose(file);
    pj_lut3d_free(lut);
    return NULL;
}

void pj_lut3d_free(PjLut3D *lut)
{
    if (!lut) return;
    free(lut->rgb);
    free(lut);
}

size_t pj_lut3d_size(const PjLut3D *lut) { return lut ? lut->size : 0; }



static const float *entry(const PjLut3D *lut, size_t r, size_t g, size_t b)
{
    return &lut->rgb[((b * lut->size + g) * lut->size + r) * 3];
}

void pj_lut3d_apply(const PjLut3D *lut, float rgb[3])
{
    if (!lut || !lut->rgb) return;
    size_t lo[3], hi[3];
    float fraction[3];
    for (size_t c = 0; c < 3; ++c) {
        float unit = (rgb[c] - lut->domain_min[c]) /
                     (lut->domain_max[c] - lut->domain_min[c]);
        if (unit < 0.0f) unit = 0.0f;
        if (unit > 1.0f) unit = 1.0f;
        float position = unit * (float)(lut->size - 1);
        lo[c] = (size_t)floorf(position);
        hi[c] = lo[c] + 1 < lut->size ? lo[c] + 1 : lo[c];
        fraction[c] = position - (float)lo[c];
    }

    /* Tetrahedral interpolation (the industry standard over trilinear):
     * the unit cube splits into six tetrahedra by the ordering of the
     * fractional coordinates; the sample is a barycentric blend of the
     * four vertices of the containing tetrahedron. Neutral axis and hue
     * behavior between lattice points are noticeably better than the
     * trilinear equivalent, at the same cost. */
    float fr = fraction[0], fg = fraction[1], fb = fraction[2];
    const float *v000 = entry(lut, lo[0], lo[1], lo[2]);
    const float *v111 = entry(lut, hi[0], hi[1], hi[2]);
    const float *va, *vb;
    float wa, wb, w0, w1;
    if (fr >= fg) {
        if (fg >= fb) {        /* r >= g >= b */
            va = entry(lut, hi[0], lo[1], lo[2]);
            vb = entry(lut, hi[0], hi[1], lo[2]);
            w0 = 1.0f - fr; wa = fr - fg; wb = fg - fb; w1 = fb;
        } else if (fr >= fb) { /* r >= b > g */
            va = entry(lut, hi[0], lo[1], lo[2]);
            vb = entry(lut, hi[0], lo[1], hi[2]);
            w0 = 1.0f - fr; wa = fr - fb; wb = fb - fg; w1 = fg;
        } else {               /* b > r >= g */
            va = entry(lut, lo[0], lo[1], hi[2]);
            vb = entry(lut, hi[0], lo[1], hi[2]);
            w0 = 1.0f - fb; wa = fb - fr; wb = fr - fg; w1 = fg;
        }
    } else {
        if (fb >= fg) {        /* b >= g > r */
            va = entry(lut, lo[0], lo[1], hi[2]);
            vb = entry(lut, lo[0], hi[1], hi[2]);
            w0 = 1.0f - fb; wa = fb - fg; wb = fg - fr; w1 = fr;
        } else if (fb >= fr) { /* g > b >= r */
            va = entry(lut, lo[0], hi[1], lo[2]);
            vb = entry(lut, lo[0], hi[1], hi[2]);
            w0 = 1.0f - fg; wa = fg - fb; wb = fb - fr; w1 = fr;
        } else {               /* g >= r > b */
            va = entry(lut, lo[0], hi[1], lo[2]);
            vb = entry(lut, hi[0], hi[1], lo[2]);
            w0 = 1.0f - fg; wa = fg - fr; wb = fr - fb; w1 = fb;
        }
    }
    float out[3];
    for (size_t c = 0; c < 3; ++c)
        out[c] = w0 * v000[c] + wa * va[c] + wb * vb[c] + w1 * v111[c];
    memcpy(rgb, out, sizeof out);
}
