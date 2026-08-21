#ifndef POLAJUICE_FILMLIB_H
#define POLAJUICE_FILMLIB_H

#include <stdbool.h>

const char *film_library_root(void);
const char *film_process_of(const char *path);
char *resolve_film(const char *request, bool quiet);
int list_films(const char *filter);

#endif
