#ifndef POLAJUICE_CLI_LOOK_H
#define POLAJUICE_CLI_LOOK_H

#include "polajuice.h"
#include <stdarg.h>

/* The camera, film and print as requested, then as resolved. The
 * request strings may point into the caller's argv or into the *_buf
 * members (look_from_params fills those). */
typedef struct {
    const char *camera, *film_request, *print_request;
    bool no_film, any_film, quiet;
    PjRenderOptions options;
    /* resolved by look_prepare */
    PjLut3D *color_lut, *print_lut;
    char film_stem[256], print_stem[256];
    char recipe[1024];
    /* storage for look_from_params */
    char camera_buf[64], film_buf[256], print_buf[256], filter_buf[32], grain_buf[16];
} Look;

typedef struct { const char *key, *value; } LookParam;

/* key=value pairs (the recipe keys) into a Look; unknown keys ignored */
bool look_from_params(Look *look, const LookParam *params, size_t count,
                      const char *tool, char *msg, size_t msgsize);
/* resolve films, check compatibility, load LUTs, write the recipe text */
bool look_prepare(Look *look, const char *tool, char *msg, size_t msgsize);
/* render (a downscaled copy when max_dim > 0), recipe embedded */
PjImage *look_render(Look *look, const PjImage *source, size_t max_dim,
                     PjError *error);
void look_release(Look *look);

#endif
