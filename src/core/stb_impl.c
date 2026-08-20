/*
 * Third-party single-header implementations are compiled in this one
 * translation unit only.  The Makefile builds this file with warnings
 * suppressed; the project's own sources keep -Wall -Wextra -Wpedantic.
 *
 * stb_image and stb_image_write are public domain (or MIT, at your
 * option); see the headers under third_party/.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_write.h"
