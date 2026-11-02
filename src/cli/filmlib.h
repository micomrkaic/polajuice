#ifndef POLAJUICE_FILMLIB_H
#define POLAJUICE_FILMLIB_H

#include <stdbool.h>

#include <stddef.h>

/* asset directories: own variable, $POLAJUICE_HOME, the source tree,
 * ~/.local/share/polajuice, the build's datadir (see filmlib.c) */
const char *asset_dir(const char *env, const char *name, const char *in_tree, char *buf, size_t n);
const char *film_library_root(void);
const char *print_library_root(void);
const char *web_root(void);
const char *film_process_of(const char *path);
char *resolve_film(const char *request, bool quiet);
int list_films(const char *filter);

#endif
