#ifndef POLAJUICE_CURVES_H
#define POLAJUICE_CURVES_H

#include <stdbool.h>
#include <stdio.h>

/* A look's color as a lattice (pj_look_cube: n points a side, sRGB in
 * and out, red fastest) with the name to print for it. */
typedef struct { const char *name; const float *cube; int n; } CurvesLook;

/* the four views as one SVG; b (may be NULL) is overlaid in thin lines */
bool curves_svg(const char *path, const CurvesLook *a, const CurvesLook *b);
/* the ColorChecker patch moves as a text table */
void curves_table(FILE *f, const CurvesLook *a, const CurvesLook *b);

#endif
