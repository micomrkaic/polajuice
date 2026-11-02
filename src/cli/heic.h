#ifndef POLAJUICE_HEIC_H
#define POLAJUICE_HEIC_H

#include <stdbool.h>
#include <stddef.h>

/* HEIC/HEIF input through an external converter when one is installed
 * (heif-convert, heif-dec, magick, convert, sips); see heic.c */
bool heic_is(const char *path);
const char *heic_converter(void);
/* converts to NAME_from_heic.jpg next to the source (reused when it is
 * not older than the source); out gets that path. False with a message
 * when no converter is installed or it failed. */
bool heic_convert(const char *path, char *out, size_t n, char *msg, size_t msgsize);

#endif
