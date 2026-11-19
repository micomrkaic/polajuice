#ifndef POLAJUICE_DTSTYLE_H
#define POLAJUICE_DTSTYLE_H

#include "polajuice.h"

/* Export a look for other programs: its color as a .cube, and for
 * darktable a .dtstyle that loads that cube in the 3D LUT module and
 * sets darktable's own grain, vignette and lowpass from the camera.
 * See dtstyle.c. Returns a process exit status. */
int export_cube(const char *camera, const PjRenderOptions *options, const char *recipe,
                const char *path, int size);
int export_darktable(const char *camera, const PjRenderOptions *options, const char *recipe,
                     const char *output, int size, bool install);

#endif
