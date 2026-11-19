/* curves.c - what a look does to color, drawn and tabulated.
 *
 * A 3D LUT is not a curve, so one chart cannot show it. Four views of
 * the look's cube (pj_look_cube: the whole color path, film included):
 *
 *   1. the neutral axis: R, G and B out against a gray ramp in - the
 *      contrast, toe and shoulder, and where the three part, the cast;
 *   2. around the hue circle at a fixed lightness and chroma: how far
 *      each hue is turned (degrees) and how its chroma is scaled;
 *   3. the a*b* plane with an arrow from each color to where it goes;
 *   4. the 24 ColorChecker patches as numbers: dL, chroma %, hue deg.
 *
 * Hue and chroma are CIELAB's (D65), which is even enough around the
 * circle for "12 degrees toward yellow" to mean the same everywhere.
 * Nothing here depends on telling colors apart: lines are labelled and
 * dashed differently, hues are named on the axis, and the table is
 * numbers. A second look (--vs) is overlaid in thin lines and gets its
 * own columns. Output is SVG written as text; the table also goes to
 * the terminal. */
#define _DEFAULT_SOURCE
#include "curves.h"

#include <errno.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- color ------------------------------------------------------------- */

static double srgb_decode(double v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double srgb_encode(double v) { return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055; }
static double lab_f(double t) { return t > 216.0 / 24389.0 ? cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0; }
static double lab_finv(double f) { double c = f * f * f; return c > 216.0 / 24389.0 ? c : (116.0 * f - 16.0) * 27.0 / 24389.0; }
static const double white[3] = {0.95047, 1.0, 1.08883};

static void srgb_to_lab(const double rgb[3], double lab[3])
{
    double r = srgb_decode(rgb[0]), g = srgb_decode(rgb[1]), b = srgb_decode(rgb[2]);
    double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
    double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
    double fx = lab_f(x / white[0]), fy = lab_f(y / white[1]), fz = lab_f(z / white[2]);
    lab[0] = 116.0 * fy - 16.0; lab[1] = 500.0 * (fx - fy); lab[2] = 200.0 * (fy - fz);
}

/* false when the color is outside sRGB */
static bool lab_to_srgb(const double lab[3], double rgb[3])
{
    double fy = (lab[0] + 16.0) / 116.0, fx = fy + lab[1] / 500.0, fz = fy - lab[2] / 200.0;
    double x = white[0] * lab_finv(fx), y = white[1] * lab_finv(fy), z = white[2] * lab_finv(fz);
    double lin[3] = { 3.2404542 * x - 1.5371385 * y - 0.4985314 * z,
                     -0.9692660 * x + 1.8760108 * y + 0.0415560 * z,
                      0.0556434 * x - 0.2040259 * y + 1.0572252 * z };
    bool inside = true;
    for (int c = 0; c < 3; ++c) {
        if (lin[c] < -1e-4 || lin[c] > 1.0 + 1e-4) inside = false;
        double v = lin[c] < 0.0 ? 0.0 : lin[c] > 1.0 ? 1.0 : lin[c];
        rgb[c] = srgb_encode(v);
    }
    return inside;
}

/* the cube, trilinear; red fastest */
static void sample(const float *cube, int n, const double in[3], double out[3])
{
    int k[3]; double f[3];
    for (int c = 0; c < 3; ++c) {
        double e = (in[c] < 0.0 ? 0.0 : in[c] > 1.0 ? 1.0 : in[c]) * (n - 1);
        k[c] = (int)e; if (k[c] > n - 2) k[c] = n - 2;
        f[c] = e - k[c];
    }
    for (int c = 0; c < 3; ++c) {
        double v = 0.0;
        for (int corner = 0; corner < 8; ++corner) {
            int dr = corner & 1, dg = (corner >> 1) & 1, db = corner >> 2;
            double w = (dr ? f[0] : 1 - f[0]) * (dg ? f[1] : 1 - f[1]) * (db ? f[2] : 1 - f[2]);
            v += w * cube[(((size_t)(k[2] + db) * n + (size_t)(k[1] + dg)) * n + (size_t)(k[0] + dr)) * 3 + c];
        }
        out[c] = v;
    }
}

static double hue_of(const double lab[3]) { double h = atan2(lab[2], lab[1]) * 180.0 / M_PI; return h < 0.0 ? h + 360.0 : h; }
static double chroma_of(const double lab[3]) { return hypot(lab[1], lab[2]); }
static double wrap180(double d) { while (d > 180.0) d -= 360.0; while (d < -180.0) d += 360.0; return d; }

/* ---- the measurements -------------------------------------------------- */

typedef struct { const char *name; unsigned char rgb[3]; } Patch;
static const Patch patches[24] = {
    {"dark skin", {115, 82, 68}}, {"light skin", {194, 150, 130}}, {"blue sky", {98, 122, 157}},
    {"foliage", {87, 108, 67}}, {"blue flower", {133, 128, 177}}, {"bluish green", {103, 189, 170}},
    {"orange", {214, 126, 44}}, {"purplish blue", {80, 91, 166}}, {"moderate red", {193, 90, 99}},
    {"purple", {94, 60, 108}}, {"yellow green", {157, 188, 64}}, {"orange yellow", {224, 163, 46}},
    {"blue", {56, 61, 150}}, {"green", {70, 148, 73}}, {"red", {175, 54, 60}},
    {"yellow", {231, 199, 31}}, {"magenta", {187, 86, 149}}, {"cyan", {8, 133, 161}},
    {"white", {243, 243, 242}}, {"neutral 8", {200, 200, 200}}, {"neutral 6.5", {160, 160, 160}},
    {"neutral 5", {122, 122, 121}}, {"neutral 3.5", {85, 85, 85}}, {"black", {52, 52, 52}},
};

/* one patch through the cube: lightness, chroma and hue moves */
static void patch_move(const float *cube, int n, const Patch *p, double out_rgb[3], double *dl, double *dc_pct, double *dh)
{
    double in[3] = { p->rgb[0] / 255.0, p->rgb[1] / 255.0, p->rgb[2] / 255.0 }, a[3], b[3];
    sample(cube, n, in, out_rgb);
    srgb_to_lab(in, a); srgb_to_lab(out_rgb, b);
    *dl = b[0] - a[0];
    double ca = chroma_of(a), cb = chroma_of(b);
    /* a neutral has no hue and no chroma to scale: report the chroma it gained */
    if (ca < 4.0) { *dc_pct = cb - ca; *dh = NAN; }
    else { *dc_pct = 100.0 * (cb / ca - 1.0); *dh = cb < 2.0 ? NAN : wrap180(hue_of(b) - hue_of(a)); }
}

void curves_table(FILE *f, const CurvesLook *a, const CurvesLook *b)
{
    fprintf(f, "%-14s  %7s %8s %7s", "patch", "dL", "chroma", "hue");
    if (b) fprintf(f, "   | %7s %8s %7s", "dL", "chroma", "hue");
    fprintf(f, "\n%-14s  %-24s", "", a->name);
    if (b) fprintf(f, "   | %s", b->name);
    fprintf(f, "\n");
    const CurvesLook *looks[2] = {a, b};
    for (int i = 0; i < 24; ++i) {
        fprintf(f, "%-14s ", patches[i].name);
        for (int k = 0; k < 2 && looks[k]; ++k) {
            double rgb[3], dl, dc, dh;
            patch_move(looks[k]->cube, looks[k]->n, &patches[i], rgb, &dl, &dc, &dh);
            if (k) fprintf(f, "   |");
            if (isnan(dh)) fprintf(f, " %+7.1f %+7.1f  %7s", dl, dc, "-");
            else fprintf(f, " %+7.1f %+7.0f%% %+6.0f\xc2\xb0", dl, dc, dh);
        }
        fprintf(f, "\n");
    }
    fprintf(f, "dL in CIELAB lightness (100 = white); chroma as %% of the patch's own (neutrals: chroma gained); hue in degrees, + toward yellow from red\n");
}

/* ---- SVG ---------------------------------------------------------------- */

static const char *font = "font-family=\"DejaVu Sans,Helvetica,Arial,sans-serif\"";

static void text(FILE *f, double x, double y, const char *anchor, int size, const char *fill, const char *s)
{
    fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"%s\" font-size=\"%d\" fill=\"%s\" %s>", x, y, anchor, size, fill, font);
    for (; *s; ++s)
        switch (*s) { case '&': fputs("&amp;", f); break; case '<': fputs("&lt;", f); break; case '>': fputs("&gt;", f); break; default: fputc(*s, f); }
    fprintf(f, "</text>\n");
}

static void frame(FILE *f, double x, double y, double w, double h, const char *title)
{
    fprintf(f, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"#fff\" stroke=\"#999\"/>\n", x, y, w, h);
    text(f, x, y - 10, "start", 15, "#111", title);
}

static void hex(const double rgb[3], char out[8])
{
    snprintf(out, 8, "#%02x%02x%02x", (int)lrint(fmin(1, fmax(0, rgb[0])) * 255), (int)lrint(fmin(1, fmax(0, rgb[1])) * 255),
             (int)lrint(fmin(1, fmax(0, rgb[2])) * 255));
}

/* 1: the neutral axis */
static void plot_neutral(FILE *f, double x, double y, double w, double h, const CurvesLook *a, const CurvesLook *b)
{
    frame(f, x, y, w, h, "1. Gray ramp: R, G, B out against gray in");
    for (int i = 1; i < 4; ++i) {
        fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#ddd\"/>\n", x + w * i / 4, y, x + w * i / 4, y + h);
        fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#ddd\"/>\n", x, y + h * i / 4, x + w, y + h * i / 4);
    }
    fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#bbb\" stroke-dasharray=\"2 4\"/>\n", x, y + h, x + w, y);
    static const char *color[3] = {"#c62828", "#2e7d32", "#1565c0"}, *dash[3] = {"", "9 4", "2 3"}, *label[3] = {"R", "G", "B"};
    const CurvesLook *looks[2] = {a, b};
    for (int k = 0; k < 2 && looks[k]; ++k)
        for (int c = 0; c < 3; ++c) {
            fprintf(f, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"%s\" stroke-dasharray=\"%s\" opacity=\"%s\" points=\"",
                    color[c], k ? "1" : "2.2", dash[c], k ? "0.75" : "1");
            double last = 0;
            for (int i = 0; i <= 64; ++i) {
                double v = i / 64.0, in[3] = {v, v, v}, out[3];
                sample(looks[k]->cube, looks[k]->n, in, out);
                fprintf(f, "%.1f,%.1f ", x + w * v, y + h * (1 - out[c]));
                last = out[c];
            }
            fprintf(f, "\"/>\n");
            if (!k) text(f, x + w + 5, y + h * (1 - last) + 4 + (c - 1) * 11, "start", 12, color[c], label[c]);
        }
    text(f, x, y + h + 16, "start", 11, "#555", "black");
    text(f, x + w / 2, y + h + 16, "middle", 11, "#555", "gray in (sRGB)");
    text(f, x + w, y + h + 16, "end", 11, "#555", "white");
    text(f, x + 8, y + 16, "start", 11, "#555", "R solid, G dashed, B dotted; diagonal = unchanged; lines apart = a color cast there");
}

/* named hues on the CIELAB circle: the sRGB primaries' and secondaries' angles */
static const struct { double h; const char *name; } hues[] = {
    {40, "red"}, {70, "orange"}, {103, "yellow"}, {136, "green"}, {196, "cyan"}, {260, "sky"}, {306, "blue"}, {328, "magenta"},
};
enum { NHUE = sizeof hues / sizeof hues[0] };

/* 2: hue turn and chroma scale around the circle */
static void plot_hue(FILE *f, double x, double y, double w, double h, const CurvesLook *a, const CurvesLook *b)
{
    frame(f, x, y, w, h, "2. Around the hue circle: hue turned, chroma scaled");
    const double L = 60.0, C = 30.0, turn_max = 30.0, gain_lo = 0.0, gain_hi = 2.0;
    fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#bbb\" stroke-dasharray=\"2 4\"/>\n", x, y + h / 2, x + w, y + h / 2);
    for (int i = 0; i < NHUE; ++i) {
        double px = x + w * hues[i].h / 360.0;
        fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#e4e4e4\"/>\n", px, y, px, y + h);
        text(f, px, y + h + 16 + (i % 2) * 13, "middle", 11, "#333", hues[i].name);
    }
    const CurvesLook *looks[2] = {a, b};
    for (int k = 0; k < 2 && looks[k]; ++k)
        for (int series = 0; series < 2; ++series) {
            fprintf(f, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"%s\" stroke-dasharray=\"%s\" opacity=\"%s\" points=\"",
                    series ? "#6a1b9a" : "#111", k ? "1" : "2.2", series ? "9 4" : "", k ? "0.75" : "1");
            for (int i = 0; i <= 120; ++i) {
                double hd = 3.0 * i, lab[3] = {L, C * cos(hd * M_PI / 180), C * sin(hd * M_PI / 180)}, in[3], out[3], ol[3];
                lab_to_srgb(lab, in);
                sample(looks[k]->cube, looks[k]->n, in, out);
                srgb_to_lab(out, ol);
                double v;
                if (series) { double g = chroma_of(ol) / C; v = (g - gain_lo) / (gain_hi - gain_lo); }
                else { double t = chroma_of(ol) < 1.0 ? 0.0 : wrap180(hue_of(ol) - hd); v = 0.5 + 0.5 * t / turn_max; }
                v = v < 0 ? 0 : v > 1 ? 1 : v;
                fprintf(f, "%.1f,%.1f ", x + w * hd / 360.0, y + h * (1 - v));
            }
            fprintf(f, "\"/>\n");
        }
    text(f, x - 6, y + 4, "end", 11, "#111", "+30\xc2\xb0");
    text(f, x - 6, y + h / 2 + 4, "end", 11, "#111", "0\xc2\xb0");
    text(f, x - 6, y + h, "end", 11, "#111", "-30\xc2\xb0");
    text(f, x + w + 6, y + 4, "start", 11, "#6a1b9a", "x2");
    text(f, x + w + 6, y + h / 2 + 4, "start", 11, "#6a1b9a", "x1");
    text(f, x + w + 6, y + h, "start", 11, "#6a1b9a", "x0");
    text(f, x + 8, y + 16, "start", 11, "#555", "solid, left scale: hue turned (+ = toward the next name to the right)");
    text(f, x + 8, y + 30, "start", 11, "#555", "dashed, right scale: chroma out / in (above x1 = more saturated)");
}

/* 3: arrows in the a*b* plane */
static void plot_plane(FILE *f, double x, double y, double w, double h, const CurvesLook *a)
{
    frame(f, x, y, w, h, "3. Color plane: where each color goes (dot = in, line end = out)");
    const double L = 60.0, span = 100.0;
    double cx = x + w / 2, cy = y + h / 2, s = (h < w ? h : w) / 2 / span;
    fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#ddd\"/>\n", x, cy, x + w, cy);
    fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#ddd\"/>\n", cx, y, cx, y + h);
    for (int ring = 1; ring <= 3; ++ring)
        fprintf(f, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"none\" stroke=\"#eee\"/>\n", cx, cy, ring * 20.0 * s);
    for (int i = 0; i < NHUE; ++i) {
        double r = 90.0 * s, t = hues[i].h * M_PI / 180;
        text(f, cx + r * cos(t), cy - r * sin(t) + 4, "middle", 11, "#333", hues[i].name);
    }
    for (int ring = 1; ring <= 3; ++ring)
        for (int i = 0; i < 24; ++i) {
            double C = ring * 20.0, t = i * 15.0 * M_PI / 180, lab[3] = {L, C * cos(t), C * sin(t)}, in[3], out[3], ol[3];
            if (!lab_to_srgb(lab, in)) continue;        /* not a color sRGB can hold */
            sample(a->cube, a->n, in, out);
            srgb_to_lab(out, ol);
            char ci[8], co[8];
            hex(in, ci); hex(out, co);
            double x0 = cx + lab[1] * s, y0 = cy - lab[2] * s, x1 = cx + ol[1] * s, y1 = cy - ol[2] * s;
            fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#222\" stroke-width=\"1.3\"/>\n", x0, y0, x1, y1);
            fprintf(f, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"3.2\" fill=\"%s\" stroke=\"#222\" stroke-width=\"0.6\"/>\n", x0, y0, ci);
            fprintf(f, "<rect x=\"%.1f\" y=\"%.1f\" width=\"5\" height=\"5\" fill=\"%s\" stroke=\"#222\" stroke-width=\"0.6\"/>\n", x1 - 2.5, y1 - 2.5, co);
        }
    text(f, x, y + h + 16, "start", 11, "#555", "a line toward the middle = less saturated; outward = more; around = hue turned");
    text(f, x, y + h + 30, "start", 11, "#555", "rings: chroma 20, 40, 60 at lightness 60");
}

/* 4: the patches as numbers */
static void plot_table(FILE *f, double x, double y, double w, double h, const CurvesLook *a, const CurvesLook *b)
{
    frame(f, x, y, w, h, "4. ColorChecker patches: in, out, and the move in numbers");
    double row = (h - 30) / 24.0, cols[4] = {x + 150, x + 215, x + 285, x + 345};
    text(f, x + 54, y + 18, "start", 11, "#555", "patch");
    text(f, cols[1], y + 18, "end", 11, "#555", "dL");
    text(f, cols[2], y + 18, "end", 11, "#555", "chroma");
    text(f, cols[3], y + 18, "end", 11, "#555", "hue");
    if (b) { text(f, cols[1] + 250, y + 18, "end", 11, "#555", "dL"); text(f, cols[2] + 250, y + 18, "end", 11, "#555", "chroma"); text(f, cols[3] + 250, y + 18, "end", 11, "#555", "hue"); }
    const CurvesLook *looks[2] = {a, b};
    for (int i = 0; i < 24; ++i) {
        double ry = y + 26 + row * i;
        double in[3] = { patches[i].rgb[0] / 255.0, patches[i].rgb[1] / 255.0, patches[i].rgb[2] / 255.0 };
        char c[8];
        hex(in, c);
        fprintf(f, "<rect x=\"%.1f\" y=\"%.1f\" width=\"20\" height=\"%.1f\" fill=\"%s\" stroke=\"#999\" stroke-width=\"0.5\"/>\n", x + 6, ry, row - 2, c);
        for (int k = 0; k < 2 && looks[k]; ++k) {
            double rgb[3], dl, dc, dh, off = k * 215.0;
            char num[32];
            patch_move(looks[k]->cube, looks[k]->n, &patches[i], rgb, &dl, &dc, &dh);
            hex(rgb, c);
            fprintf(f, "<rect x=\"%.1f\" y=\"%.1f\" width=\"20\" height=\"%.1f\" fill=\"%s\" stroke=\"#999\" stroke-width=\"0.5\"/>\n",
                    k ? cols[0] + 250 - 24 : x + 28, ry, row - 2, c);
            if (!k) text(f, x + 54, ry + row - 5, "start", 11, "#111", patches[i].name);
            snprintf(num, sizeof num, "%+.1f", dl); text(f, cols[1] + off, ry + row - 5, "end", 11, "#111", num);
            if (isnan(dh)) { snprintf(num, sizeof num, "%+.1f", dc); text(f, cols[2] + off, ry + row - 5, "end", 11, "#111", num); text(f, cols[3] + off, ry + row - 5, "end", 11, "#111", "-"); }
            else {
                snprintf(num, sizeof num, "%+.0f%%", dc); text(f, cols[2] + off, ry + row - 5, "end", 11, "#111", num);
                snprintf(num, sizeof num, "%+.0f\xc2\xb0", dh); text(f, cols[3] + off, ry + row - 5, "end", 11, "#111", num);
            }
        }
    }
}

/* ---- by wavelength ----------------------------------------------------- *
 * What a physicist asks: what does the look do to light of 550 nm? An
 * RGB pipeline never sees a wavelength - a camera has reduced the
 * spectrum to three numbers before any film emulation starts - so the
 * question is put the only way it can be: each wavelength is replaced by
 * the color a display would use for it (its chromaticity pulled toward
 * white until sRGB can hold it, at a fixed luminance), and that color is
 * followed through the look. Reported per wavelength:
 *   - how much lighter or darker it comes out than a gray of the same
 *     luminance (stops) - the tone curve cancels, so this is the look's
 *     spectral weighting relative to the eye's, V(lambda); for a
 *     black-and-white film, its rendered spectral sensitivity;
 *   - the dominant wavelength of what comes out, and the shift (nm);
 *   - its excitation purity against what went in.
 * It is the response to a metamer, not a spectral sensitivity measured
 * on film: nothing outside what sRGB encodes (the infrared an IR stock
 * lives on, say) is in a LUT. CIE 1931 observer by the multi-lobe fit of
 * Wyman, Sloan and Shirley (2013); white is D65. */

static double lobe(double l, double mu, double s1, double s2) { double t = (l - mu) / (l < mu ? s1 : s2); return exp(-0.5 * t * t); }
static void cmf(double l, double xyz[3])
{
    xyz[0] = 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2);
    xyz[1] = 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);
    xyz[2] = 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);
}
static void locus_xy(double l, double xy[2]) { double v[3]; cmf(l, v); double sum = v[0] + v[1] + v[2]; xy[0] = v[0] / sum; xy[1] = v[1] / sum; }
static const double white_xy[2] = {0.3127, 0.3290};

static void xyY_to_linear(const double xy[2], double Y, double lin[3])
{
    double X = xy[0] * Y / xy[1], Z = (1.0 - xy[0] - xy[1]) * Y / xy[1];
    lin[0] = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
    lin[1] = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
    lin[2] = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
}
static void encoded_to_xyY(const double rgb[3], double xy[2], double *Y)
{
    double r = srgb_decode(rgb[0]), g = srgb_decode(rgb[1]), b = srgb_decode(rgb[2]);
    double X = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
    *Y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    double Z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b, sum = X + *Y + Z;
    if (sum < 1e-9) { xy[0] = white_xy[0]; xy[1] = white_xy[1]; } else { xy[0] = X / sum; xy[1] = *Y / sum; }
}

enum { SPEC_LO = 410, SPEC_HI = 690 };
/* The stimulus: D65 white with monochromatic light of wavelength l mixed
 * in to an excitation purity of 0.20, at the luminance of a middle gray.
 * 0.20 is what sRGB can hold at every wavelength (its green-cyan edge
 * near 510 nm reaches 0.22), so every wavelength is treated alike and
 * the curves are smooth; a purer stimulus would have to be clipped to
 * the gamut differently at each wavelength. */
static const double spec_purity = 0.20, spec_luminance = 0.18;

static void spectral_standin(double l, double rgb[3])
{
    double s[2], lin[3];
    locus_xy(l, s);
    double c[2] = { white_xy[0] + spec_purity * (s[0] - white_xy[0]), white_xy[1] + spec_purity * (s[1] - white_xy[1]) };
    xyY_to_linear(c, spec_luminance, lin);
    for (int k = 0; k < 3; ++k) rgb[k] = srgb_encode(fmin(1.0, fmax(0.0, lin[k])));
}

/* Hue as a wavelength is only well defined where the spectral locus
 * moves: below about 445 nm and above about 645 nm the chromaticity of
 * monochromatic light all but stops changing (and the analytic observer
 * is least accurate there), so dominant wavelengths are sought, and
 * stimuli made, between these two. */
#define HUE_LO 445.0
#define HUE_HI 645.0

/* dominant wavelength and excitation purity of a chromaticity; false
 * for a neutral or a purple (no single wavelength has that hue) */
static bool dominant(const double xy[2], double *wavelength, double *purity)
{
    double dx = xy[0] - white_xy[0], dy = xy[1] - white_xy[1], dist = hypot(dx, dy);
    if (dist < 0.004) return false;
    double angle = atan2(dy, dx), best = 1e9, best_l = 0.0;
    for (double l = HUE_LO; l <= HUE_HI; l += 0.25) {
        double s[2];
        locus_xy(l, s);
        double d = fabs(wrap180((atan2(s[1] - white_xy[1], s[0] - white_xy[0]) - angle) * 180.0 / M_PI));
        if (d < best) { best = d; best_l = l; }
    }
    if (best > 0.6) return false;                       /* the ray leaves through the purple line */
    double s[2];
    locus_xy(best_l, s);
    *wavelength = best_l;
    *purity = dist / hypot(s[0] - white_xy[0], s[1] - white_xy[1]);
    return true;
}

/* The look's sensitivity of rendered luminance to each sRGB primary,
 * around a middle gray: central differences on the cube, in linear
 * light. Three numbers are all an RGB pipeline has of a spectrum, and
 * with them its spectral sensitivity is fixed: S(l) = g . M cmf(l), a
 * combination of the observer's three matching functions. For the
 * identity S is the eye's own V(l). */
static void luminance_gradient(const CurvesLook *look, double g[3])
{
    const double base = spec_luminance, step = 0.04;
    for (int c = 0; c < 3; ++c) {
        double y[2];
        for (int side = 0; side < 2; ++side) {
            double lin[3] = {base, base, base}, enc[3], out[3], xy[2];
            lin[c] += side ? step : -step;
            for (int k = 0; k < 3; ++k) enc[k] = srgb_encode(lin[k]);
            sample(look->cube, look->n, enc, out);
            encoded_to_xyY(out, xy, &y[side]);
        }
        g[c] = (y[1] - y[0]) / (2.0 * step);
    }
}

/* spectral sensitivity of the rendered luminance at wavelength l, scaled
 * so that the identity gives V(l); and V(l) itself */
static double sensitivity(const double g[3], double l, double *eye)
{
    double v[3], lin[3];
    cmf(l, v);
    lin[0] = 3.2404542 * v[0] - 1.5371385 * v[1] - 0.4985314 * v[2];
    lin[1] = -0.9692660 * v[0] + 1.8760108 * v[1] + 0.0415560 * v[2];
    lin[2] = 0.0556434 * v[0] - 0.2040259 * v[1] + 1.0572252 * v[2];
    if (eye) *eye = v[1];
    double white = g[0] + g[1] + g[2];
    return fabs(white) < 1e-9 ? 0.0 : (g[0] * lin[0] + g[1] * lin[1] + g[2] * lin[2]) / white;
}

typedef struct { double stops, dominant_in, dominant_out, purity_in, purity_out; bool hue_in, hue_out; double rgb_in[3], rgb_out[3]; } SpecPoint;

static void spectral_point(const CurvesLook *look, double l, SpecPoint *p)
{
    double xy[2], y_in, y_out;
    spectral_standin(l, p->rgb_in);
    encoded_to_xyY(p->rgb_in, xy, &y_in);
    p->hue_in = l >= HUE_LO && l <= HUE_HI && dominant(xy, &p->dominant_in, &p->purity_in);
    sample(look->cube, look->n, p->rgb_in, p->rgb_out);
    encoded_to_xyY(p->rgb_out, xy, &y_out);
    p->purity_out = 0.0;
    p->hue_out = p->hue_in && dominant(xy, &p->dominant_out, &p->purity_out);
    if (p->hue_in && !p->hue_out) p->purity_out = hypot(xy[0] - white_xy[0], xy[1] - white_xy[1]) < 0.004 ? 0.0 : -1.0;   /* neutral : purple */
    (void)y_out;
    double g[3], eye, sens;
    luminance_gradient(look, g);
    sens = sensitivity(g, l, &eye);
    p->stops = sens > 0.0 && eye > 1e-6 ? log2(sens / eye) : NAN;   /* NAN: no or negative response */
}

void curves_spectral_table(FILE *f, const CurvesLook *a, const CurvesLook *b)
{
    fprintf(f, "\n%-10s  %8s %9s %7s %8s", "wavelength", "vs eye", "comes out", "shift", "purity");
    if (b) fprintf(f, "   | %8s %9s %7s %8s", "vs eye", "comes out", "shift", "purity");
    fprintf(f, "\n%-10s  %-36s", "", a->name);
    if (b) fprintf(f, "  | %s", b->name);
    fprintf(f, "\n");
    const CurvesLook *looks[2] = {a, b};
    for (int l = 420; l <= 680; l += 20) {
        fprintf(f, "%4d nm    ", l);
        for (int k = 0; k < 2 && looks[k]; ++k) {
            SpecPoint p;
            spectral_point(looks[k], l, &p);
            if (k) fprintf(f, "   |");
            if (isnan(p.stops)) fprintf(f, " %8s", "none"); else fprintf(f, " %+5.2f st", p.stops);
            if (p.hue_out && p.hue_in) fprintf(f, "   %4.0f nm %+4.0f nm    x%.2f", p.dominant_out, p.dominant_out - p.dominant_in, p.purity_out / p.purity_in);
            else fprintf(f, " %9s %7s %8s", !p.hue_in ? "-" : p.purity_out < 0.0 ? "purple" : "neutral", "-", "-");
        }
        fprintf(f, "\n");
    }
    fprintf(f, "vs eye: the look's spectral sensitivity of rendered brightness (around a middle gray) against the eye's V(lambda), in stops:\n"
               "+1 = light of that wavelength comes out a stop lighter than the eye would weigh it; none = no or negative response.\n"
               "comes out / shift / purity: white with monochromatic light of that wavelength mixed in (excitation purity %.2f, luminance\n"
               "%.2f - the purest sRGB holds at every wavelength) followed through the look: dominant wavelength of the result, its shift,\n"
               "and excitation purity out over in; given for 445-645 nm, outside which the hue of light barely changes with wavelength.\n"
               "An RGB pipeline has three numbers of a spectrum, so its sensitivity is a combination of the observer's three matching\n"
               "functions - not a film's measured curve, and blind to anything sRGB does not encode (infrared, say).\n",
            spec_purity, spec_luminance);
}

static double spec_x(double x, double w, double l) { return x + w * (l - SPEC_LO) / (double)(SPEC_HI - SPEC_LO); }

static void spec_axis(FILE *f, double x, double y, double w, double h)
{
    for (int l = 450; l <= 650; l += 50) {
        char label[16];
        fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#e4e4e4\"/>\n", spec_x(x, w, l), y, spec_x(x, w, l), y + h);
        snprintf(label, sizeof label, "%d nm", l);
        text(f, spec_x(x, w, l), y + h + 30, "middle", 11, "#333", label);
    }
    /* what was fed in: the stand-in for each wavelength */
    for (int l = SPEC_LO; l < SPEC_HI; l += 5) {
        double rgb[3]; char c[8];
        spectral_standin(l + 2.5, rgb);
        hex(rgb, c);
        fprintf(f, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"10\" fill=\"%s\"/>\n", spec_x(x, w, l), y + h + 4, w * 5.0 / (SPEC_HI - SPEC_LO) + 0.5, c);
    }
}

/* 5: spectral sensitivity of the rendered brightness, with the eye's */
static void plot_spec_gain(FILE *f, double x, double y, double w, double h, const CurvesLook *a, const CurvesLook *b)
{
    frame(f, x, y, w, h, "5. Spectral sensitivity of the rendered brightness (dotted: the eye)");
    spec_axis(f, x, y, w, h);
    /* scale: the eye peaks near 1; leave room above and a little below zero */
    const double lo = -0.4, hi = 2.0;
    double zero = y + h * (1 - (0.0 - lo) / (hi - lo));
    fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#bbb\"/>\n", x, zero, x + w, zero);
    text(f, x - 6, zero + 4, "end", 11, "#111", "0");
    text(f, x - 6, y + h * (1 - (1.0 - lo) / (hi - lo)) + 4, "end", 11, "#111", "1");
    text(f, x - 6, y + h * (1 - (2.0 - lo) / (hi - lo)) + 10, "end", 11, "#111", "2");
    fprintf(f, "<polyline fill=\"none\" stroke=\"#555\" stroke-width=\"1.4\" stroke-dasharray=\"2 3\" points=\"");
    for (int l = SPEC_LO; l <= SPEC_HI; l += 5) {
        double v[3];
        cmf(l, v);
        fprintf(f, "%.1f,%.1f ", spec_x(x, w, l), y + h * (1 - (v[1] - lo) / (hi - lo)));
    }
    fprintf(f, "\"/>\n");
    const CurvesLook *looks[2] = {a, b};
    for (int k = 0; k < 2 && looks[k]; ++k) {
        double g[3];
        luminance_gradient(looks[k], g);
        fprintf(f, "<polyline fill=\"none\" stroke=\"#111\" stroke-width=\"%s\" opacity=\"%s\" points=\"", k ? "1" : "2.2", k ? "0.75" : "1");
        for (int l = SPEC_LO; l <= SPEC_HI; l += 5) {
            double v = (sensitivity(g, l, NULL) - lo) / (hi - lo);
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            fprintf(f, "%.1f,%.1f ", spec_x(x, w, l), y + h * (1 - v));
        }
        fprintf(f, "\"/>\n");
    }
    text(f, x, y + h + 46, "start", 11, "#555", "how strongly each wavelength drives the result's brightness, around a middle gray");
}

/* 6: the color that comes out, by wavelength */
static void plot_spec_hue(FILE *f, double x, double y, double w, double h, const CurvesLook *a, const CurvesLook *b)
{
    frame(f, x, y, w, h, "6. By wavelength: dominant wavelength shifted (nm), purity scaled");
    const double shift_max = 40.0, gain_hi = 2.0;
    spec_axis(f, x, y, w, h);
    fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#bbb\" stroke-dasharray=\"2 4\"/>\n", x, y + h / 2, x + w, y + h / 2);
    const CurvesLook *looks[2] = {a, b};
    bool any = false;
    for (int k = 0; k < 2 && looks[k]; ++k)
        for (int series = 0; series < 2; ++series) {
            bool open = false;
            for (int l = SPEC_LO; l <= SPEC_HI; l += 5) {
                SpecPoint p;
                spectral_point(looks[k], l, &p);
                bool ok = p.hue_in && p.hue_out && p.purity_out > 0.02;
                if (series) ok = p.hue_in && p.purity_out >= 0.0;   /* purity is defined down to zero, not for a purple */
                if (!ok) { if (open) { fprintf(f, "\"/>\n"); open = false; } continue; }
                if (!open) {
                    fprintf(f, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"%s\" stroke-dasharray=\"%s\" opacity=\"%s\" points=\"",
                            series ? "#6a1b9a" : "#111", k ? "1" : "2.2", series ? "9 4" : "", k ? "0.75" : "1");
                    open = true; any = true;
                }
                double v = series ? p.purity_out / p.purity_in / gain_hi : 0.5 + 0.5 * (p.dominant_out - p.dominant_in) / shift_max;
                v = v < 0 ? 0 : v > 1 ? 1 : v;
                fprintf(f, "%.1f,%.1f ", spec_x(x, w, l), y + h * (1 - v));
            }
            if (open) fprintf(f, "\"/>\n");
        }
    (void)any;
    text(f, x - 6, y + 4, "end", 11, "#111", "+40");
    text(f, x - 6, y + h / 2 + 4, "end", 11, "#111", "0");
    text(f, x - 6, y + h, "end", 11, "#111", "-40");
    text(f, x + w + 6, y + 4, "start", 11, "#6a1b9a", "x2");
    text(f, x + w + 6, y + h / 2 + 4, "start", 11, "#6a1b9a", "x1");
    text(f, x + w + 6, y + h, "start", 11, "#6a1b9a", "x0");
    text(f, x, y + h + 46, "start", 11, "#555", "solid, left: nm the hue moved (+ = toward red); dashed, right: purity out / in");
}

bool curves_svg(const char *path, const CurvesLook *a, const CurvesLook *b)
{
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "polajuice: cannot write %s: %s\n", path, strerror(errno)); return false; }
    const double W = 1280, H = 1450;
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n", W, H, W, H);
    fprintf(f, "<rect width=\"100%%\" height=\"100%%\" fill=\"#f6f4ee\"/>\n");
    char title[600];
    if (b) snprintf(title, sizeof title, "%s   (thick lines, first columns)   against   %s   (thin lines, second columns)", a->name, b->name);
    else snprintf(title, sizeof title, "%s", a->name);
    text(f, 40, 30, "start", 17, "#111", title);
    plot_neutral(f, 50, 80, 520, 380, a, b);
    plot_hue(f, 50, 540, 520, 380, a, b);
    plot_plane(f, 660, 80, 380, 380, a);
    plot_table(f, 660, 540, b ? 610 : 365, 440, a, b);
    plot_spec_gain(f, 50, 1040, 520, 300, a, b);
    plot_spec_hue(f, 660, 1040, 520, 300, a, b);
    text(f, 40, H - 30, "start", 11, "#666", "polajuice curves: the look's color path through a 33-point lattice, sRGB in and out; hue and chroma in CIELAB (D65).");
    text(f, 40, H - 14, "start", 11, "#666", "By wavelength (CIE 1931 observer): an RGB pipeline holds three numbers of a spectrum, so these are what the look does to a camera's rendering of each wavelength - not a film's measured curves.");
    fprintf(f, "</svg>\n");
    return fclose(f) == 0;
}
