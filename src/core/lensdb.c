/* lensdb.c - lens profiles: what a known lens does to the frame at a
 * focal length and aperture, as the distortion and devignette options
 * undo it.
 *
 * A lens is keyed by its specification (min and max focal length, the
 * maximum aperture at each), which every EXIF writer records and which
 * needs no decryption, unlike Nikon's LensData. The figures are
 * approximate: corner distortion as a fraction of the half-diagonal
 * (positive barrel, so 0.033 is the 3.3% barrel of a 24 mm wide end;
 * the distortion option's r' = r(1 - k r^2) puts the corner back by that
 * fraction) and corner falloff in stops wide open, read off published
 * test charts (Photozone/OpticalLimits, Lenstip, DxOMark) and rounded.
 * Falloff at smaller apertures follows (f_open / f)^1.5, floored at a
 * fifth of the wide-open value, which tracks those charts to a quarter
 * stop. They are a starting point the sliders then refine, not a
 * calibration. */
#include "polajuice.h"

#include <math.h>
#include <string.h>

typedef struct { float focal, distortion, falloff; } LensPoint;
typedef struct {
    const char *name;
    float min_focal, max_focal, max_ap_min, max_ap_max;
    const LensPoint *points;   /* ascending focal length */
    int count;
} LensProfile;

static const LensPoint nikkor_24_120_f4[] = {
    {24.0f, 0.033f, 2.0f}, {35.0f, 0.005f, 1.3f}, {50.0f, -0.008f, 1.2f},
    {70.0f, -0.013f, 1.3f}, {120.0f, -0.012f, 1.5f},
};
static const LensPoint nikkor_50_f14g[] = { {50.0f, 0.011f, 2.2f} };
static const LensPoint nikkor_18_55_vr[] = {
    {18.0f, 0.030f, 1.2f}, {24.0f, 0.010f, 1.0f}, {35.0f, -0.005f, 0.9f}, {55.0f, -0.010f, 0.8f},
};
static const LensPoint nikkor_z_24_70_f4[] = {
    {24.0f, 0.040f, 2.2f}, {35.0f, 0.010f, 1.5f}, {50.0f, -0.005f, 1.4f}, {70.0f, -0.010f, 1.6f},
};
static const LensPoint nikkor_z_40_f2[] = { {40.0f, 0.020f, 1.8f} };
static const LensPoint nikkor_z_28_f28[] = { {28.0f, 0.035f, 1.6f} };

#define PROFILE(n, a, b, c, d, pts) { n, a, b, c, d, pts, (int)(sizeof pts / sizeof pts[0]) }
static const LensProfile profiles[] = {
    PROFILE("AF-S NIKKOR 24-120mm f/4G ED VR", 24.0f, 120.0f, 4.0f, 4.0f, nikkor_24_120_f4),
    PROFILE("AF-S NIKKOR 50mm f/1.4G", 50.0f, 50.0f, 1.4f, 1.4f, nikkor_50_f14g),
    PROFILE("AF-S DX NIKKOR 18-55mm f/3.5-5.6G VR", 18.0f, 55.0f, 3.5f, 5.6f, nikkor_18_55_vr),
    PROFILE("NIKKOR Z 24-70mm f/4 S", 24.0f, 70.0f, 4.0f, 4.0f, nikkor_z_24_70_f4),
    PROFILE("NIKKOR Z 40mm f/2", 40.0f, 40.0f, 2.0f, 2.0f, nikkor_z_40_f2),
    PROFILE("NIKKOR Z 28mm f/2.8", 28.0f, 28.0f, 2.8f, 2.8f, nikkor_z_28_f28),
};

static bool near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

const char *pj_lens_profile(const PjRawLens *lens, float *distortion, float *devignette)
{
    if (!lens || !lens->have) return NULL;
    const LensProfile *p = NULL;
    for (size_t i = 0; i < sizeof profiles / sizeof profiles[0]; ++i) {
        const LensProfile *q = &profiles[i];
        if (near(lens->min_focal, q->min_focal, 0.6f) && near(lens->max_focal, q->max_focal, 0.6f) &&
            near(lens->max_ap_min, q->max_ap_min, 0.15f) && near(lens->max_ap_max, q->max_ap_max, 0.15f)) {
            p = q;
            break;
        }
    }
    if (!p) return NULL;
    float f = lens->focal > 0.0f ? lens->focal : p->min_focal;
    if (f < p->points[0].focal) f = p->points[0].focal;
    if (f > p->points[p->count - 1].focal) f = p->points[p->count - 1].focal;
    float dist = p->points[0].distortion, fall = p->points[0].falloff;
    for (int i = 0; i + 1 < p->count; ++i) {
        const LensPoint *a = &p->points[i], *b = &p->points[i + 1];
        if (f >= a->focal && f <= b->focal) {
            float t = b->focal > a->focal ? (f - a->focal) / (b->focal - a->focal) : 0.0f;
            dist = a->distortion + (b->distortion - a->distortion) * t;
            fall = a->falloff + (b->falloff - a->falloff) * t;
            break;
        }
    }
    /* the maximum aperture at this focal length, for the falloff's decay */
    float ap_open = p->max_ap_min;
    if (p->max_focal > p->min_focal)
        ap_open += (p->max_ap_max - p->max_ap_min) * (f - p->min_focal) / (p->max_focal - p->min_focal);
    if (lens->fnumber > ap_open && ap_open > 0.0f) {
        float k = powf(ap_open / lens->fnumber, 1.5f);
        if (k < 0.2f) k = 0.2f;
        fall *= k;
    }
    /* rounded to the page's slider steps, so the applied profile and the
     * recipe say the same thing; the table is no finer than that anyway */
    if (distortion) *distortion = roundf(dist * 100.0f) / 100.0f;
    if (devignette) *devignette = roundf(fall * 10.0f) / 10.0f;
    return p->name;
}
