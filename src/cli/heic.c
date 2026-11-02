/* heic.c - HEIC/HEIF input through an external converter.
 *
 * A HEIC is an HEVC video frame in an ISO-BMFF box. Decoding it means an
 * HEVC decoder, which is the one thing this project will not carry;
 * instead, when a converter is installed - heif-convert or heif-dec
 * (libheif), magick or convert (ImageMagick), sips (macOS) - the file
 * is converted once to NAME_from_heic.jpg next to the source and that
 * JPEG is what gets loaded. Nothing here is required: without a
 * converter the file is refused with the ways in. POSIX (fork/exec,
 * stat); linked into the CLIs only, never into the wasm. */
#define _POSIX_C_SOURCE 200809L

#include "polajuice.h"
#include "heic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

bool heic_is(const char *path)
{
    if (!path) return false;
    const char *dot = strrchr(path, '.');
    if (dot && (!strcasecmp(dot, ".heic") || !strcasecmp(dot, ".heif") || !strcasecmp(dot, ".hif")))
        return true;
    return pj_image_is_heif(path);
}

/* the derived JPEG's path: NAME_from_heic.jpg beside the source */
static bool derived_path(const char *path, char *out, size_t n)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (!dot || (slash && dot < slash)) dot = path + strlen(path);
    int k = snprintf(out, n, "%.*s_from_heic.jpg", (int)(dot - path), path);
    return k > 0 && (size_t)k < n;
}

typedef struct { const char *tool; const char *argv[12]; } Converter;
/* "%i" and "%o" stand for the input and output paths */
static const Converter converters[] = {
    { "heif-convert", { "heif-convert", "-q", "95", "%i", "%o", NULL } },
    { "heif-dec",     { "heif-dec", "-q", "95", "%i", "%o", NULL } },
    { "magick",       { "magick", "%i", "-quality", "95", "%o", NULL } },
    { "sips",         { "sips", "-s", "format", "jpeg", "-s", "formatOptions", "95", "%i", "--out", "%o", NULL } },
    { "convert",      { "convert", "%i", "-quality", "95", "%o", NULL } },
};

static bool on_path(const char *tool)
{
    const char *path = getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    char buf[4096];
    while (*path) {
        const char *colon = strchr(path, ':');
        size_t len = colon ? (size_t)(colon - path) : strlen(path);
        if (len && len + strlen(tool) + 2 < sizeof buf) {
            snprintf(buf, sizeof buf, "%.*s/%s", (int)len, path, tool);
            if (access(buf, X_OK) == 0) return true;
        }
        if (!colon) break;
        path = colon + 1;
    }
    return false;
}

const char *heic_converter(void)
{
    for (size_t i = 0; i < sizeof converters / sizeof converters[0]; ++i)
        if (on_path(converters[i].tool)) return converters[i].tool;
    return NULL;
}

static bool run(const Converter *c, const char *in, const char *out)
{
    const char *argv[12];
    int n = 0;
    for (; c->argv[n] && n < 11; ++n)
        argv[n] = !strcmp(c->argv[n], "%i") ? in : !strcmp(c->argv[n], "%o") ? out : c->argv[n];
    argv[n] = NULL;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        /* the converter's chatter goes nowhere; errors come back as its status */
        FILE *null = freopen("/dev/null", "w", stdout);
        (void)null;
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool heic_convert(const char *path, char *out, size_t n, char *msg, size_t msgsize)
{
    if (!derived_path(path, out, n)) { snprintf(msg, msgsize, "path too long: %s", path); return false; }
    struct stat src, dst;
    if (stat(path, &src) != 0) { snprintf(msg, msgsize, "cannot read '%s'", path); return false; }
    /* a conversion already made, and not older than the file it came from */
    if (stat(out, &dst) == 0 && dst.st_size > 0 && dst.st_mtime >= src.st_mtime) return true;
    for (size_t i = 0; i < sizeof converters / sizeof converters[0]; ++i) {
        if (!on_path(converters[i].tool)) continue;
        if (run(&converters[i], path, out) && stat(out, &dst) == 0 && dst.st_size > 0) return true;
        remove(out);
        snprintf(msg, msgsize, "%s could not convert '%s'", converters[i].tool, path);
        return false;
    }
    snprintf(msg, msgsize,
             "'%s' is HEIC, an HEVC image, which polajuice does not decode itself. "
             "Install libheif-examples (heif-convert) or ImageMagick and it is converted "
             "on the way in; or convert it by hand (heif-convert, magick, sips -s format jpeg "
             "on macOS), or shoot JPEG / ProRAW DNG", path);
    return false;
}
