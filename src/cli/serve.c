/*
 * polajuice serve [DIR] [--port N] [--web PATH]
 *
 * A small HTTP server on the loopback interface that serves the
 * workspace page and takes over its rendering: the page probes
 * /api/version on load and, finding the native engine, sends every
 * render here instead of to the serial wasm build. Threaded renders,
 * the whole film library (resolved the way the CLI resolves it), the
 * images of a folder listed in the strip, exports written next to the
 * source with the recipe embedded, and looks kept as .recipe files in
 * looks/ under the working directory, shared through the repo.
 *
 * POSIX sockets, one request at a time (renders are threaded inside),
 * HTTP/1.1 with Connection: close. Binds 127.0.0.1 only: this is a
 * local tool, not a service.
 *
 *   GET  /api/version                     {"version","threads","dir","native":true}
 *   GET  /api/cameras                     the camera list, as the page needs it
 *   GET  /api/files                       images in DIR
 *   GET  /api/file?name=X                 the bytes of one
 *   GET  /api/develop?name=X&max=N        a raw file's straight print (JPEG)
 *   POST /api/render?name=X&max=N&camera=..&film=..&...   JPEG
 *        (without name: the request body is the image, ext=jpg|png|nef|dng)
 *   POST /api/export?name=X&camera=..&...  full size, written next to X
 *   POST /api/batch?camera=..&...          every image in DIR, written next to it
 *   GET  /api/looks                        the .recipe files in looks/
 *   PUT  /api/looks?name=X   (body: text)  save one
 *   DELETE /api/looks?name=X               remove one
 *   GET  /films/films.json                 the staged manifest, or one made from the library
 *   GET  /films/STEM.cube                  a cube, resolved through the library
 *   GET  /anything-else                    a file under web/
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include "polajuice.h"
#include "internal.h"
#include "filmlib.h"
#include "look.h"
#include "recipe.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_HEADER 16384
#define MAX_PARAMS 48

typedef struct {
    char method[8];
    char path[1024];          /* decoded, without the query */
    char query[2048];         /* raw query string */
    unsigned char *body;
    size_t body_len;
    LookParam params[MAX_PARAMS];
    char param_store[4096];
    size_t nparams;
} Request;

static const char *g_dir = ".";
static const char *g_web = "web";

/* ---- small helpers ----------------------------------------------------- */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s)
{
    char *out = s;
    for (; *s; ++s) {
        if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
            *out++ = (char)(hexval(s[1]) * 16 + hexval(s[2]));
            s += 2;
        } else if (*s == '+') *out++ = ' ';
        else *out++ = *s;
    }
    *out = '\0';
}

static void parse_query(Request *r)
{
    r->nparams = 0;
    size_t used = 0;
    char *q = r->query;
    while (*q && r->nparams < MAX_PARAMS) {
        char *amp = strchr(q, '&');
        size_t len = amp ? (size_t)(amp - q) : strlen(q);
        if (used + len + 1 >= sizeof r->param_store) break;
        char *item = r->param_store + used;
        memcpy(item, q, len);
        item[len] = '\0';
        used += len + 1;
        char *eq = strchr(item, '=');
        if (eq) { *eq = '\0'; url_decode(eq + 1); r->params[r->nparams].value = eq + 1; }
        else r->params[r->nparams].value = "";
        url_decode(item);
        r->params[r->nparams].key = item;
        r->nparams++;
        q = amp ? amp + 1 : q + len;
    }
}

static const char *param(const Request *r, const char *key)
{
    for (size_t i = 0; i < r->nparams; ++i)
        if (!strcmp(r->params[i].key, key)) return r->params[i].value;
    return NULL;
}

static bool safe_name(const char *name)
{
    /* one path component, no dot-dot, nothing hidden */
    if (!name || !*name || name[0] == '.' || strlen(name) > 255) return false;
    for (const char *p = name; *p; ++p)
        if (*p == '/' || *p == '\\' || *p < 32) return false;
    return true;
}

static bool is_image_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") || !strcasecmp(dot, ".png") ||
           !strcasecmp(dot, ".nef") || !strcasecmp(dot, ".dng") || !strcasecmp(dot, ".ppm");
}

/* NAME_camera.jpg is one of our exports: left out of the strip and of
 * batch runs, so a folder can be re-run without rendering renders */
static bool is_export_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || (strcasecmp(dot, ".jpg") && strcasecmp(dot, ".jpeg"))) return false;
    for (size_t i = 0; i < pj_preset_count(); ++i) {
        const char *cam = pj_preset_name(i);
        size_t cl = strlen(cam), nl = (size_t)(dot - name);
        if (nl > cl + 1 && name[nl - cl - 1] == '_' && !strncmp(name + nl - cl, cam, cl)) return true;
    }
    return false;
}

static const char *ext_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : "";
}

static void json_escape(const char *s, char *out, size_t size)
{
    size_t k = 0;
    for (; *s && k + 7 < size; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[k++] = '\\'; out[k++] = (char)c; }
        else if (c == '\n') { out[k++] = '\\'; out[k++] = 'n'; }
        else if (c < 32) k += (size_t)snprintf(out + k, size - k, "\\u%04x", c);
        else out[k++] = (char)c;
    }
    out[k] = '\0';
}

/* ---- responses --------------------------------------------------------- */

static bool send_all(int fd, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) { if (errno == EINTR) continue; return false; }
        p += (size_t)n; len -= (size_t)n;
    }
    return true;
}

static void respond(int fd, int status, const char *type, const void *body, size_t len)
{
    const char *text = status == 200 ? "OK" : status == 404 ? "Not Found"
                     : status == 400 ? "Bad Request" : status == 405 ? "Method Not Allowed"
                     : "Internal Server Error";
    char head[512];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, text, type, len);
    send_all(fd, head, (size_t)n);
    if (len) send_all(fd, body, len);
}

static void respond_text(int fd, int status, const char *text)
{
    respond(fd, status, "text/plain; charset=utf-8", text, strlen(text));
}

static void respond_json(int fd, const char *json)
{
    respond(fd, 200, "application/json", json, strlen(json));
}

static void respond_error(int fd, int status, const char *msg)
{
    char esc[1024], json[1100];
    json_escape(msg, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"error\":\"%s\"}", esc);
    respond(fd, status, "application/json", json, strlen(json));
}

static bool respond_file(int fd, const char *path, const char *type)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return false; }
    rewind(f);
    unsigned char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return false; }
    fclose(f);
    respond(fd, 200, type, buf, (size_t)n);
    free(buf);
    return true;
}

static const char *mime_of(const char *path)
{
    const char *e = ext_of(path);
    if (!strcasecmp(e, "html")) return "text/html; charset=utf-8";
    if (!strcasecmp(e, "js") || !strcasecmp(e, "mjs")) return "text/javascript";
    if (!strcasecmp(e, "wasm")) return "application/wasm";
    if (!strcasecmp(e, "json")) return "application/json";
    if (!strcasecmp(e, "jpg") || !strcasecmp(e, "jpeg")) return "image/jpeg";
    if (!strcasecmp(e, "png")) return "image/png";
    if (!strcasecmp(e, "css")) return "text/css";
    if (!strcasecmp(e, "cube") || !strcasecmp(e, "txt") || !strcasecmp(e, "recipe")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

/* a JPEG from an image, in memory: via a temp file, the encoder writes
 * paths */
static unsigned char *jpeg_bytes(PjImage *image, size_t *len, PjError *error)
{
    char path[256];
    snprintf(path, sizeof path, "/tmp/pj_serve_%ld_%d.jpg", (long)getpid(), rand());
    if (!pj_image_save(image, path, error)) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) { remove(path); pj_set_error(error, "cannot reopen render"); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    unsigned char *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    remove(path);
    if (!buf) { pj_set_error(error, "cannot read render back"); return NULL; }
    *len = (size_t)n;
    return buf;
}

/* ---- the image behind a request ---------------------------------------- */

static PjImage *load_request_image(const Request *r, char *msg, size_t msgsize)
{
    PjError error = {{0}};
    const char *name = param(r, "name");
    if (name) {
        if (!safe_name(name) || !is_image_name(name)) { snprintf(msg, msgsize, "bad file name"); return NULL; }
        char path[2048];
        snprintf(path, sizeof path, "%s/%s", g_dir, name);
        PjImage *image = pj_image_load(path, &error);
        if (!image) snprintf(msg, msgsize, "%s", error.message);
        return image;
    }
    if (!r->body || !r->body_len) { snprintf(msg, msgsize, "no image: give name= or a body"); return NULL; }
    const char *ext = param(r, "ext");
    if (!ext || !*ext) ext = "jpg";
    char path[256];
    snprintf(path, sizeof path, "/tmp/pj_serve_in_%ld.%s", (long)getpid(), ext);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(r->body, 1, r->body_len, f) != r->body_len) { if (f) fclose(f); snprintf(msg, msgsize, "cannot stage upload"); return NULL; }
    fclose(f);
    PjImage *image = pj_image_load(path, &error);
    remove(path);
    if (!image) snprintf(msg, msgsize, "%s", error.message);
    return image;
}

/* ---- handlers ------------------------------------------------------------ */

static void handle_version(int fd)
{
    char esc[2048], json[2400];
    json_escape(g_dir, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"version\":\"%s\",\"threads\":%u,\"dir\":\"%s\",\"native\":true}",
             pj_version_string(), pj_thread_count(), esc);
    respond_json(fd, json);
}

static void handle_cameras(int fd)
{
    size_t size = 65536;
    char *json = malloc(size);
    size_t k = (size_t)snprintf(json, size, "[");
    for (size_t i = 0; i < pj_preset_count(); ++i) {
        const char *name = pj_preset_name(i);
        const char *film = pj_preset_default_film(name);
        char traits[512], procs[128] = "";
        pj_preset_traits(name, traits, sizeof traits);
        static const char *all[] = {"slide", "negative", "bw", "integral", "pack"};
        size_t used = 0;
        for (size_t t = 0; t < 5; ++t)
            if (pj_preset_accepts_film(name, all[t])) {
                int n = snprintf(procs + used, sizeof procs - used, "%s\"%s\"", used ? "," : "", all[t]);
                if (n > 0) used += (size_t)n;
            }
        char nesc[200], fesc[300], desc[600], tesc[1100];
        json_escape(name, nesc, sizeof nesc);
        json_escape(film ? film : "", fesc, sizeof fesc);
        json_escape(pj_preset_description(name), desc, sizeof desc);
        json_escape(traits, tesc, sizeof tesc);
        if (k + 2600 >= size) { size *= 2; json = realloc(json, size); }
        const char *format = pj_preset_format(name);
        char fmtesc[100];
        json_escape(format ? format : "", fmtesc, sizeof fmtesc);
        k += (size_t)snprintf(json + k, size - k,
                              "%s{\"name\":\"%s\",\"film\":%s%s%s,\"description\":\"%s\","
                              "\"traits\":\"%s\",\"instant\":%s,\"processes\":[%s],"
                              "\"format\":%s%s%s}",
                              i ? "," : "", nesc, film ? "\"" : "", film ? fesc : "null", film ? "\"" : "",
                              desc, tesc, pj_preset_is_instant(name) ? "true" : "false", procs,
                              format ? "\"" : "", format ? fmtesc : "null", format ? "\"" : "");
    }
    snprintf(json + k, size - k, "]");
    respond_json(fd, json);
    free(json);
}

static int name_compare(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

static void handle_files(int fd)
{
    DIR *d = opendir(g_dir);
    if (!d) { respond_error(fd, 500, "cannot read the folder"); return; }
    char **names = NULL;
    size_t count = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !is_image_name(e->d_name) || is_export_name(e->d_name)) continue;
        if (count == cap) { cap = cap ? cap * 2 : 64; names = realloc(names, cap * sizeof *names); }
        names[count++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, count, sizeof *names, name_compare);
    size_t size = 64 + count * 400;
    char *json = malloc(size);
    size_t k = (size_t)snprintf(json, size, "[");
    for (size_t i = 0; i < count; ++i) {
        char path[2048], esc[600];
        snprintf(path, sizeof path, "%s/%s", g_dir, names[i]);
        struct stat st;
        long long bytes = stat(path, &st) == 0 ? (long long)st.st_size : 0;
        json_escape(names[i], esc, sizeof esc);
        k += (size_t)snprintf(json + k, size - k, "%s{\"name\":\"%s\",\"ext\":\"%s\",\"size\":%lld}",
                              i ? "," : "", esc, ext_of(names[i]), bytes);
        free(names[i]);
    }
    free(names);
    snprintf(json + k, size - k, "]");
    respond_json(fd, json);
    free(json);
}

static void handle_file(int fd, const Request *r)
{
    const char *name = param(r, "name");
    if (!safe_name(name) || !is_image_name(name)) { respond_error(fd, 400, "bad file name"); return; }
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    if (!respond_file(fd, path, mime_of(name))) respond_error(fd, 404, "no such file");
}

static void handle_develop(int fd, const Request *r)
{
    char msg[512];
    PjImage *image = load_request_image(r, msg, sizeof msg);
    if (!image) { respond_error(fd, 400, msg); return; }
    PjError error = {{0}};
    const char *max = param(r, "max");
    image = pj_image_downscale(image, max ? (size_t)strtoull(max, NULL, 10) : 0, &error);
    if (!image) { respond_error(fd, 500, error.message); return; }
    pj_image_scene_to_display(image, 0.0f);
    size_t len = 0;
    unsigned char *jpg = jpeg_bytes(image, &len, &error);
    pj_image_free(image);
    if (!jpg) { respond_error(fd, 500, error.message); return; }
    respond(fd, 200, "image/jpeg", jpg, len);
    free(jpg);
}

/* render one image with the look in the query; full size when max is
 * 0 or absent */
static PjImage *render_with_params(const Request *r, PjImage *source, size_t max_dim,
                                   Look *look, char *msg, size_t msgsize)
{
    if (!look_from_params(look, r->params, r->nparams, "polajuice serve", msg, msgsize))
        return NULL;
    if (!look_prepare(look, "polajuice serve", msg, msgsize)) return NULL;
    PjError error = {{0}};
    PjImage *result = look_render(look, source, max_dim, &error);
    look_release(look);
    if (!result) snprintf(msg, msgsize, "%s", error.message);
    return result;
}

static void handle_render(int fd, const Request *r)
{
    char msg[512];
    PjImage *source = load_request_image(r, msg, sizeof msg);
    if (!source) { respond_error(fd, 400, msg); return; }
    const char *max = param(r, "max");
    size_t max_dim = max ? (size_t)strtoull(max, NULL, 10) : 0;
    Look look;
    PjImage *result = render_with_params(r, source, max_dim, &look, msg, sizeof msg);
    pj_image_free(source);
    if (!result) { respond_error(fd, 400, msg); return; }
    PjError error = {{0}};
    size_t len = 0;
    unsigned char *jpg = jpeg_bytes(result, &len, &error);
    pj_image_free(result);
    if (!jpg) { respond_error(fd, 500, error.message); return; }
    respond(fd, 200, "image/jpeg", jpg, len);
    free(jpg);
}

/* IMG_1234.DNG + super8 -> IMG_1234_super8.jpg next to it; a second
 * export of the same look overwrites, a different camera gets its own */
static void export_path(const char *name, const char *camera, char *out, size_t size)
{
    const char *dot = strrchr(name, '.');
    size_t stem = dot ? (size_t)(dot - name) : strlen(name);
    snprintf(out, size, "%s/%.*s_%s.jpg", g_dir, (int)stem, name, camera);
}

static bool export_one(const Request *r, const char *name, char *written, size_t wsize,
                       char *msg, size_t msgsize)
{
    PjError error = {{0}};
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    PjImage *source = pj_image_load(path, &error);
    if (!source) { snprintf(msg, msgsize, "%s", error.message); return false; }
    Look look;
    PjImage *result = render_with_params(r, source, 0, &look, msg, msgsize);
    pj_image_free(source);
    if (!result) return false;
    export_path(name, look.camera, written, wsize);
    bool ok = pj_image_save(result, written, &error);
    pj_image_free(result);
    if (!ok) snprintf(msg, msgsize, "%s", error.message);
    return ok;
}

static void handle_export(int fd, const Request *r)
{
    const char *name = param(r, "name");
    if (!safe_name(name) || !is_image_name(name)) { respond_error(fd, 400, "bad file name"); return; }
    char written[2048], msg[512], esc[2200], json[2400];
    if (!export_one(r, name, written, sizeof written, msg, sizeof msg)) { respond_error(fd, 400, msg); return; }
    json_escape(written, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"written\":\"%s\"}", esc);
    respond_json(fd, json);
}

static void handle_batch(int fd, const Request *r)
{
    DIR *d = opendir(g_dir);
    if (!d) { respond_error(fd, 500, "cannot read the folder"); return; }
    size_t size = 65536;
    char *json = malloc(size);
    size_t k = (size_t)snprintf(json, size, "{\"written\":[");
    size_t count = 0, failed = 0;
    char errs[4096] = "";
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !is_image_name(e->d_name) || is_export_name(e->d_name)) continue;
        char written[2048], msg[512], esc[2200];
        if (!export_one(r, e->d_name, written, sizeof written, msg, sizeof msg)) {
            ++failed;
            size_t el = strlen(errs);
            snprintf(errs + el, sizeof errs - el, "%s%s: %s", el ? "; " : "", e->d_name, msg);
            continue;
        }
        json_escape(written, esc, sizeof esc);
        if (k + strlen(esc) + 16 >= size) { size *= 2; json = realloc(json, size); }
        k += (size_t)snprintf(json + k, size - k, "%s\"%s\"", count ? "," : "", esc);
        ++count;
    }
    closedir(d);
    char eesc[4200];
    json_escape(errs, eesc, sizeof eesc);
    if (k + strlen(eesc) + 64 >= size) { size += strlen(eesc) + 64; json = realloc(json, size); }
    snprintf(json + k, size - k, "],\"count\":%zu,\"failed\":%zu,\"errors\":\"%s\"}", count, failed, eesc);
    respond_json(fd, json);
    free(json);
}

static void looks_dir(char *out, size_t size)
{
    snprintf(out, size, "looks");
}

static void handle_looks(int fd, const Request *r)
{
    char dir[256];
    looks_dir(dir, sizeof dir);
    if (!strcmp(r->method, "GET")) {
        DIR *d = opendir(dir);
        size_t size = 65536;
        char *json = malloc(size);
        size_t k = (size_t)snprintf(json, size, "[");
        size_t count = 0;
        if (d) {
            struct dirent *e;
            char **names = NULL; size_t n = 0, cap = 0;
            while ((e = readdir(d))) {
                const char *dot = strrchr(e->d_name, '.');
                if (e->d_name[0] == '.' || !dot || strcmp(dot, ".recipe")) continue;
                if (n == cap) { cap = cap ? cap * 2 : 32; names = realloc(names, cap * sizeof *names); }
                names[n++] = strdup(e->d_name);
            }
            closedir(d);
            qsort(names, n, sizeof *names, name_compare);
            for (size_t i = 0; i < n; ++i) {
                char path[512];
                snprintf(path, sizeof path, "%s/%s", dir, names[i]);
                FILE *f = fopen(path, "rb");
                if (!f) { free(names[i]); continue; }
                char text[4096];
                size_t got = fread(text, 1, sizeof text - 1, f);
                fclose(f);
                text[got] = '\0';
                names[i][strlen(names[i]) - 7] = '\0';   /* drop .recipe */
                char nesc[600], tesc[8400];
                json_escape(names[i], nesc, sizeof nesc);
                json_escape(text, tesc, sizeof tesc);
                if (k + strlen(nesc) + strlen(tesc) + 40 >= size) { size *= 2; json = realloc(json, size); }
                k += (size_t)snprintf(json + k, size - k, "%s{\"name\":\"%s\",\"text\":\"%s\"}",
                                      count ? "," : "", nesc, tesc);
                ++count;
                free(names[i]);
            }
            free(names);
        }
        snprintf(json + k, size - k, "]");
        respond_json(fd, json);
        free(json);
        return;
    }
    const char *name = param(r, "name");
    if (!safe_name(name) || strlen(name) > 120) { respond_error(fd, 400, "bad look name"); return; }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.recipe", dir, name);
    if (!strcmp(r->method, "PUT") || !strcmp(r->method, "POST")) {
        if (!r->body || !r->body_len) { respond_error(fd, 400, "no recipe text"); return; }
        mkdir(dir, 0755);
        FILE *f = fopen(path, "wb");
        if (!f || fwrite(r->body, 1, r->body_len, f) != r->body_len) {
            if (f) fclose(f);
            respond_error(fd, 500, "cannot write the look");
            return;
        }
        fclose(f);
        respond_json(fd, "{\"ok\":true}");
        return;
    }
    if (!strcmp(r->method, "DELETE")) {
        if (remove(path) != 0) { respond_error(fd, 404, "no such look"); return; }
        respond_json(fd, "{\"ok\":true}");
        return;
    }
    respond_error(fd, 405, "GET, PUT or DELETE");
}

/* films.json: the staged manifest when the web tree has one, else a
 * manifest made from the library's directory layout, so that a fresh
 * clone with 'make fetch-luts' has every stock in the dropdown */
static const char *process_label(const char *token)
{
    if (!token) return "";
    if (!strcmp(token, "slide")) return "E-6";
    if (!strcmp(token, "negative")) return "C-41";
    if (!strcmp(token, "bw")) return "B+W";
    if (!strcmp(token, "integral") || !strcmp(token, "pack")) return "internal";
    if (!strcmp(token, "print")) return "print";
    return "";
}

static void handle_films_manifest(int fd)
{
    char staged[2048];
    snprintf(staged, sizeof staged, "%s/films/films.json", g_web);
    if (respond_file(fd, staged, "application/json")) return;
    const char *root = film_library_root();
    DIR *d = opendir(root);
    if (!d) { respond_json(fd, "{\"families\":[]}"); return; }   /* no library: an empty, valid manifest */
    size_t size = 131072;
    char *json = malloc(size);
    size_t k = (size_t)snprintf(json, size, "{\"families\":[");
    size_t fams = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char fdir[2304];
        snprintf(fdir, sizeof fdir, "%s/%s", root, e->d_name);
        struct stat st;
        if (stat(fdir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        DIR *fd2 = opendir(fdir);
        if (!fd2) continue;
        char **stems = NULL; size_t n = 0, cap = 0;
        struct dirent *f;
        const char *token = NULL;
        while ((f = readdir(fd2))) {
            const char *dot = strrchr(f->d_name, '.');
            if (f->d_name[0] == '.' || !dot || strcasecmp(dot, ".cube")) continue;
            if (n == cap) { cap = cap ? cap * 2 : 64; stems = realloc(stems, cap * sizeof *stems); }
            stems[n] = strdup(f->d_name);
            stems[n][strlen(stems[n]) - 5] = '\0';
            if (!token) {
                char path[2600];
                snprintf(path, sizeof path, "%s/%s", fdir, f->d_name);
                token = film_process_of(path);
            }
            ++n;
        }
        closedir(fd2);
        if (!n) { free(stems); continue; }
        qsort(stems, n, sizeof *stems, name_compare);
        char nesc[600];
        json_escape(e->d_name, nesc, sizeof nesc);
        const char *label = process_label(token);
        k += (size_t)snprintf(json + k, size - k, "%s{\"name\":\"%s%s\",\"process\":\"%s\",\"note\":\"\",\"stocks\":[",
                              fams ? "," : "", nesc,
                              token && !strcmp(token, "pack") ? " (pack film)" : "", label);
        for (size_t i = 0; i < n; ++i) {
            char sesc[600];
            json_escape(stems[i], sesc, sizeof sesc);
            if (k + strlen(sesc) + 32 >= size) { size *= 2; json = realloc(json, size); }
            k += (size_t)snprintf(json + k, size - k, "%s{\"stem\":\"%s\",\"blurb\":\"\"}", i ? "," : "", sesc);
            free(stems[i]);
        }
        free(stems);
        k += (size_t)snprintf(json + k, size - k, "]}");
        ++fams;
    }
    closedir(d);
    /* print stocks live beside the library */
    snprintf(json + k, size - k, "]}");
    respond_json(fd, json);
    free(json);
}

static void handle_cube(int fd, const char *stem)
{
    if (!safe_name(stem)) { respond_error(fd, 400, "bad stem"); return; }
    char *path = resolve_film(stem, true);
    if (!path) { respond_error(fd, 404, "no such film"); return; }
    bool ok = respond_file(fd, path, "text/plain; charset=utf-8");
    free(path);
    if (!ok) respond_error(fd, 404, "no such film");
}

static void handle_static(int fd, const char *path)
{
    if (strstr(path, "..")) { respond_error(fd, 400, "no"); return; }
    char full[4096];
    const char *rel = (!*path || !strcmp(path, "/")) ? "index.html" : path + 1;
    if (strlen(g_web) + strlen(rel) + 2 > sizeof full) { respond_text(fd, 404, "not found"); return; }
    strcpy(full, g_web);
    strcat(full, "/");
    strcat(full, rel);
    if (!respond_file(fd, full, mime_of(full))) respond_text(fd, 404, "not found");
}

/* ---- the request loop ---------------------------------------------------- */

static bool read_request(int fd, Request *r)
{
    static char head[MAX_HEADER + 1];
    size_t got = 0;
    char *end = NULL;
    while (got < MAX_HEADER) {
        ssize_t n = read(fd, head + got, MAX_HEADER - got);
        if (n <= 0) return false;
        got += (size_t)n;
        head[got] = '\0';
        end = strstr(head, "\r\n\r\n");
        if (end) break;
    }
    if (!end) return false;
    size_t header_len = (size_t)(end + 4 - head);
    char *line_end = strstr(head, "\r\n");
    *line_end = '\0';
    char target[1024];
    if (sscanf(head, "%7s %1023s", r->method, target) != 2) return false;
    char *q = strchr(target, '?');
    if (q) { *q = '\0'; snprintf(r->query, sizeof r->query, "%s", q + 1); } else r->query[0] = '\0';
    memcpy(r->path, target, sizeof target);
    url_decode(r->path);
    parse_query(r);
    /* content length */
    size_t content_length = 0;
    for (char *h = line_end + 2; h < end; ) {
        char *nl = strstr(h, "\r\n");
        if (!nl) break;
        *nl = '\0';
        if (!strncasecmp(h, "Content-Length:", 15)) content_length = (size_t)strtoull(h + 15, NULL, 10);
        h = nl + 2;
    }
    r->body = NULL; r->body_len = 0;
    if (content_length) {
        if (content_length > (size_t)512 * 1024 * 1024) return false;
        r->body = malloc(content_length);
        if (!r->body) return false;
        size_t have = got - header_len;
        if (have > content_length) have = content_length;
        memcpy(r->body, head + header_len, have);
        while (have < content_length) {
            ssize_t n = read(fd, r->body + have, content_length - have);
            if (n <= 0) { free(r->body); r->body = NULL; return false; }
            have += (size_t)n;
        }
        r->body_len = content_length;
    }
    return true;
}

static void handle(int fd, Request *r)
{
    const char *p = r->path;
    if (!strcmp(p, "/api/version")) handle_version(fd);
    else if (!strcmp(p, "/api/cameras")) handle_cameras(fd);
    else if (!strcmp(p, "/api/files")) handle_files(fd);
    else if (!strcmp(p, "/api/file")) handle_file(fd, r);
    else if (!strcmp(p, "/api/develop")) handle_develop(fd, r);
    else if (!strcmp(p, "/api/render")) handle_render(fd, r);
    else if (!strcmp(p, "/api/export")) handle_export(fd, r);
    else if (!strcmp(p, "/api/batch")) handle_batch(fd, r);
    else if (!strcmp(p, "/api/looks")) handle_looks(fd, r);
    else if (!strcmp(p, "/films/films.json")) handle_films_manifest(fd);
    else if (!strncmp(p, "/films/", 7) && strlen(p) > 12 && !strcmp(p + strlen(p) - 5, ".cube")) {
        char stem[300];
        snprintf(stem, sizeof stem, "%.*s", (int)(strlen(p) - 12), p + 7);
        handle_cube(fd, stem);
    }
    else if (!strncmp(p, "/api/", 5)) respond_error(fd, 404, "unknown endpoint");
    else handle_static(fd, p);
}

int serve_main(int argc, char **argv)
{
    int port = 8000;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--web") && i + 1 < argc) g_web = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "polajuice serve: unknown option %s\n", argv[i]); return EXIT_FAILURE; }
        else g_dir = argv[i];
    }
    struct stat st;
    if (stat(g_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "polajuice serve: '%s' is not a directory\n", g_dir);
        return EXIT_FAILURE;
    }
    char index[2048];
    snprintf(index, sizeof index, "%s/index.html", g_web);
    if (stat(index, &st) != 0) {
        fprintf(stderr, "polajuice serve: no page at %s (run from the repo, or --web PATH)\n", index);
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return EXIT_FAILURE; }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) != 0) {
        fprintf(stderr, "polajuice serve: cannot bind port %d (%s)\n", port, strerror(errno));
        return EXIT_FAILURE;
    }
    if (listen(sock, 16) != 0) { perror("listen"); return EXIT_FAILURE; }
    printf("polajuice serve: http://localhost:%d/  folder %s  (%u threads; Ctrl-C to stop)\n",
           port, g_dir, pj_thread_count());
    fflush(stdout);
    for (;;) {
        int fd = accept(sock, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; break; }
        Request r;
        memset(&r, 0, sizeof r);
        if (read_request(fd, &r)) handle(fd, &r);
        free(r.body);
        close(fd);
    }
    close(sock);
    return EXIT_SUCCESS;
}
