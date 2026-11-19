/* dtstyle.c - a look as a darktable style.
 *
 * A style is a list of darktable modules with their parameters. The
 * look's color - white balance, exposure, the film (LUT or scalar
 * engine), the print stock, development, age, the projected color of a
 * Super 8 frame - is baked into one .cube by running a lattice through
 * the pipeline (pj_look_cube), and the style points darktable's 3D LUT
 * module at it. What a lattice cannot carry goes to darktable's own
 * modules, set from the camera: grain, vignette, and for a format that
 * states its resolution a lowpass of that width. Halation, edge
 * effects, the instant frame and the scene's graduated and radial
 * filters have no counterpart and are named in the style's description.
 *
 * Module parameters are the modules' C structs, hex-encoded, tagged
 * with the struct's version; the layouts below are darktable 4.6's
 * (lut3d v3, grain v2, vignette v4, lowpass v4), which newer releases
 * read through their legacy-parameter upgrade. Blend parameters are
 * left empty: darktable then takes the module's defaults.
 *
 * The 3D LUT module resolves its file against the "3D lut root folder"
 * preference, so the cube has to live under that folder; --install
 * reads the folder from darktablerc and puts both files in place. */
#define _POSIX_C_SOURCE 200809L

#include "dtstyle.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- the cube ---------------------------------------------------------- */

static bool write_cube(const char *path, const char *title, const float *rgb, int n)
{
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "polajuice: cannot write %s: %s\n", path, strerror(errno)); return false; }
    fprintf(f, "# polajuice %s - the color of a look; sRGB in, sRGB out\n", pj_version_string());
    fprintf(f, "TITLE \"%s\"\nLUT_3D_SIZE %d\nDOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n", title, n);
    size_t count = (size_t)n * (size_t)n * (size_t)n;
    for (size_t i = 0; i < count; ++i)
        fprintf(f, "%.6f %.6f %.6f\n", (double)rgb[i * 3], (double)rgb[i * 3 + 1], (double)rgb[i * 3 + 2]);
    return fclose(f) == 0;
}

static float *make_cube(const char *camera, const PjRenderOptions *options, int size)
{
    PjError error = {{0}};
    float *rgb = malloc((size_t)size * (size_t)size * (size_t)size * 3 * sizeof *rgb);
    if (!rgb) { fprintf(stderr, "polajuice: out of memory\n"); return NULL; }
    if (!pj_look_cube(camera, options, size, rgb, &error)) {
        fprintf(stderr, "polajuice: %s\n", error.message);
        free(rgb);
        return NULL;
    }
    return rgb;
}

/* the file's name without directory and extension, as the look's name */
static void stem_of(const char *path, char *out, size_t n)
{
    const char *slash = strrchr(path, '/');
    snprintf(out, n, "%s", slash ? slash + 1 : path);
    char *dot = strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
}

int export_cube(const char *camera, const PjRenderOptions *options, const char *recipe,
                const char *path, int size)
{
    (void)recipe;
    float *rgb = make_cube(camera, options, size);
    if (!rgb) return EXIT_FAILURE;
    char name[256];
    stem_of(path, name, sizeof name);
    bool ok = write_cube(path, name, rgb, size);
    free(rgb);
    if (ok) printf("%s  (%d^3, sRGB in and out: color only - grain, softness, halation and the frame are not in a LUT)\n", path, size);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* ---- the style --------------------------------------------------------- */

typedef struct { unsigned char *data; size_t len, cap; } Blob;

static void put(Blob *b, const void *src, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2;
        b->data = realloc(b->data, b->cap);
        if (!b->data) { fprintf(stderr, "polajuice: out of memory\n"); exit(EXIT_FAILURE); }
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
}
/* little-endian, as darktable writes its structs on every platform it runs on */
static void put_i32(Blob *b, int32_t v)
{
    unsigned char raw[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    put(b, raw, 4);
}
static void put_f32(Blob *b, float v) { int32_t i; memcpy(&i, &v, 4); put_i32(b, i); }
static void put_text(Blob *b, const char *text, size_t field)
{
    size_t n = strlen(text);
    if (n >= field) n = field - 1;
    put(b, text, n);
    static const unsigned char zero[64] = {0};
    for (size_t left = field - n; left; ) { size_t k = left < sizeof zero ? left : sizeof zero; put(b, zero, k); left -= k; }
}

static void xml_text(FILE *f, const char *text)
{
    for (; *text; ++text)
        switch (*text) {
        case '&': fputs("&amp;", f); break;
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        case '"': fputs("&quot;", f); break;
        case '\n': fputs("; ", f); break;
        default: fputc(*text, f);
        }
}

static void plugin(FILE *f, int num, const char *operation, int version, const Blob *params, const char *label)
{
    fprintf(f, "  <plugin>\n    <num>%d</num>\n    <module>%d</module>\n    <operation>%s</operation>\n    <op_params>",
            num, version, operation);
    for (size_t i = 0; i < params->len; ++i) fprintf(f, "%02x", params->data[i]);
    fprintf(f, "</op_params>\n    <enabled>1</enabled>\n    <blendop_params></blendop_params>\n"
               "    <blendop_version>0</blendop_version>\n    <multi_priority>0</multi_priority>\n    <multi_name>");
    xml_text(f, label);
    fprintf(f, "</multi_name>\n    <multi_name_hand_edited>1</multi_name_hand_edited>\n  </plugin>\n");
}

/* dt_iop_lut3d_params_t v3: filepath[512], colorspace, interpolation,
 * nb_keypoints, c_clut[2048*2*3], lutname[128] */
static void lut3d_params(Blob *b, const char *relative_path)
{
    put_text(b, relative_path, 512);
    put_i32(b, 0);              /* DT_IOP_SRGB: the cube is sRGB in, sRGB out */
    put_i32(b, 0);              /* DT_IOP_TETRAHEDRAL */
    put_i32(b, 0);              /* no compressed lut */
    put_text(b, "", 2048 * 2 * 3);
    put_text(b, "", 128);
}

/* dt_iop_grain_params_t v2: channel, scale, strength, midtones_bias.
 * polajuice's grain is a display-referred deviation (0.024 on a 35mm
 * negative) and darktable's strength a percentage whose default 25 is
 * a like amount; its coarseness is an ISO over 213.2, and a grain_scale
 * of 1 px on a 24 MP frame sits near ISO 400, growing with the area. */
static void grain_params(Blob *b, const PjPresetCharacter *c, float strength)
{
    /* calibrated against darktable 4.6 on a 24 MP frame: matching
     * high-frequency deviation for the fine-grained cameras, and by eye
     * for Super 8, where darktable's coarse grain reads stronger */
    float coarse = c->grain_scale > 1.0f ? c->grain_scale : 1.0f;
    float amount = c->grain * 850.0f * strength / sqrtf(coarse);
    if (amount > 100.0f) amount = 100.0f;
    float iso = 400.0f * powf(coarse, 2.3f);
    if (iso < 100.0f) iso = 100.0f;
    if (iso > 6400.0f) iso = 6400.0f;
    put_i32(b, 2);              /* DT_GRAIN_CHANNEL_LIGHTNESS */
    put_f32(b, iso / 213.2f);
    put_f32(b, amount);
    put_f32(b, fminf(100.0f, fmaxf(0.0f, c->grain_midtone_bias * 100.0f)));
}

/* dt_iop_vignette_params_t v4: scale, falloff_scale, brightness,
 * saturation, center{x,y}, autoratio, whratio, shape, dithering, unbound */
static void vignette_params(Blob *b, const PjPresetCharacter *c, float strength)
{
    float dark = c->vignette * 2.5f * strength;
    if (dark > 1.0f) dark = 1.0f;
    put_f32(b, 60.0f);          /* fall-off start, % of the largest dimension */
    put_f32(b, 70.0f);          /* fall-off radius */
    put_f32(b, -dark);
    put_f32(b, -0.3f * dark);
    put_f32(b, 0.0f); put_f32(b, 0.0f);
    put_i32(b, 1);              /* automatic ratio: follow the frame */
    put_f32(b, 1.0f);
    put_f32(b, 1.0f);
    put_i32(b, 1);              /* dither for 8-bit output */
    put_i32(b, 1);              /* unbound */
}

/* dt_iop_lowpass_params_t v4: order, radius, contrast, brightness,
 * saturation, lowpass_algo, unbound. The radius is a sigma in pixels of
 * the full image; a format that resolves `cycles` across the frame at
 * MTF50 needs sigma = 0.1874 L / cycles, here for a 6000-pixel frame
 * (a style cannot follow the image; scale the radius for other sizes). */
static float lowpass_radius(const PjPresetCharacter *c) { return 0.1874f * 6000.0f / c->resolve; }
static void lowpass_params(Blob *b, const PjPresetCharacter *c)
{
    put_i32(b, 0);
    put_f32(b, lowpass_radius(c));
    put_f32(b, 1.0f);
    put_f32(b, 0.0f);
    put_f32(b, 1.0f);
    put_i32(b, 0);              /* gaussian */
    put_i32(b, 1);
}

static bool write_style(const char *path, const char *name, const char *cube_relative, const char *camera,
                        const PjRenderOptions *options, const char *recipe)
{
    PjPresetCharacter c;
    if (!pj_preset_character(camera, &c)) return false;
    float strength = options ? options->strength : 1.0f;
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "polajuice: cannot write %s: %s\n", path, strerror(errno)); return false; }
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<darktable_style version=\"1.0\">\n<info>\n  <name>");
    xml_text(f, name);
    fprintf(f, "</name>\n  <description>");
    char note[1024];
    snprintf(note, sizeof note,
             "polajuice %s look. Color in the 3D LUT (%s under the 3D lut root folder); grain%s%s from the camera. "
             "Not carried: halation%s%s%s. Apply after your own development of the picture. Recipe: ",
             pj_version_string(), cube_relative,
             c.vignette > 0.0f ? ", vignette" : "", c.resolve > 0.0f ? ", lowpass (sized for a 6000 px frame)" : "",
             c.instant ? ", the instant print's frame" : "",
             c.flash ? ", the direct flash (its falloff across the frame)" : "",
             options && (options->grad != 0.0f || options->radial != 0.0f || options->edge != 0.0f)
                 ? ", graduated/radial filters, edge effects" : "");
    xml_text(f, note);
    xml_text(f, recipe ? recipe : "");
    fprintf(f, "</description>\n</info>\n<style>\n");
    int num = 0;
    Blob b = {0};
    if (c.resolve > 0.0f && strength > 0.0f) {
        lowpass_params(&b, &c);
        plugin(f, num++, "lowpass", 4, &b, "polajuice format");
        b.len = 0;
    }
    lut3d_params(&b, cube_relative);
    plugin(f, num++, "lut3d", 3, &b, "polajuice look");
    b.len = 0;
    if (c.vignette > 0.0f && strength > 0.0f) {
        vignette_params(&b, &c, strength);
        plugin(f, num++, "vignette", 4, &b, "polajuice");
        b.len = 0;
    }
    if (c.grain > 0.0f && strength > 0.0f) {
        grain_params(&b, &c, strength);
        plugin(f, num++, "grain", 2, &b, "polajuice");
        b.len = 0;
    }
    free(b.data);
    fprintf(f, "</style>\n</darktable_style>\n");
    return fclose(f) == 0;
}

/* ---- darktable's folders ---------------------------------------------- */

static bool config_dir(char *out, size_t n)
{
    const char *over = getenv("POLAJUICE_DARKTABLE_CONFIG");   /* a test's or an odd install's */
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (over && *over) snprintf(out, n, "%s", over);
    else if (xdg && *xdg) snprintf(out, n, "%s/darktable", xdg);
    else if (home && *home) snprintf(out, n, "%s/.config/darktable", home);
    else return false;
    return true;
}

/* plugins/darkroom/lut3d/def_path from darktablerc */
static bool lut_root(const char *config, char *out, size_t n)
{
    char rc[4096], line[4096];
    snprintf(rc, sizeof rc, "%s/darktablerc", config);
    FILE *f = fopen(rc, "r");
    if (!f) return false;
    static const char key[] = "plugins/darkroom/lut3d/def_path=";
    bool found = false;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, sizeof key - 1)) {
            line[strcspn(line, "\r\n")] = '\0';
            snprintf(out, n, "%s", line + sizeof key - 1);
            found = out[0] != '\0';
        }
    fclose(f);
    return found;
}

static bool mkdir_p(const char *dir)
{
    char buf[4300];
    snprintf(buf, sizeof buf, "%s", dir);
    for (char *p = buf + 1; *p; ++p)
        if (*p == '/') { *p = '\0'; mkdir(buf, 0755); *p = '/'; }
    return mkdir(buf, 0755) == 0 || errno == EEXIST;
}

int export_darktable(const char *camera, const PjRenderOptions *options, const char *recipe,
                     const char *output, int size, bool install)
{
    char name[200], cube_path[8192], style_path[8192], relative[512];
    stem_of(output, name, sizeof name);
    /* the directory the user named, for the files when not installing */
    char dir[4096] = ".";
    const char *slash = strrchr(output, '/');
    if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - output), output);

    char config[4096] = "", root[4096] = "";
    if (install) {
        if (!config_dir(config, sizeof config)) { fprintf(stderr, "polajuice: no HOME to find darktable's configuration in\n"); return EXIT_FAILURE; }
        if (!lut_root(config, root, sizeof root)) {
            fprintf(stderr,
                    "polajuice: darktable has no 3D lut root folder set (looked in %s/darktablerc).\n"
                    "  Set one with darktable closed, for instance:\n"
                    "    mkdir -p ~/Pictures/luts && echo 'plugins/darkroom/lut3d/def_path=%s/Pictures/luts' >> %s/darktablerc\n"
                    "  or in darktable: preferences > processing > 3D lut root folder. Then run this again.\n",
                    config, getenv("HOME") ? getenv("HOME") : "~", config);
            return EXIT_FAILURE;
        }
        char sub[4200];
        snprintf(sub, sizeof sub, "%s/polajuice", root);
        snprintf(cube_path, sizeof cube_path, "%s/%s.cube", sub, name);
        snprintf(relative, sizeof relative, "polajuice/%s.cube", name);
        char styles[4200];
        snprintf(styles, sizeof styles, "%s/styles", config);
        snprintf(style_path, sizeof style_path, "%s/%s.dtstyle", styles, name);
        if (!mkdir_p(sub) || !mkdir_p(styles)) { fprintf(stderr, "polajuice: cannot create %s or %s\n", sub, styles); return EXIT_FAILURE; }
    } else {
        snprintf(cube_path, sizeof cube_path, "%s/%s.cube", dir, name);
        snprintf(style_path, sizeof style_path, "%s/%s.dtstyle", dir, name);
        snprintf(relative, sizeof relative, "%s.cube", name);
    }

    float *rgb = make_cube(camera, options, size);
    if (!rgb) return EXIT_FAILURE;
    bool ok = write_cube(cube_path, name, rgb, size);
    free(rgb);
    if (!ok || !write_style(style_path, name, relative, camera, options, recipe)) return EXIT_FAILURE;

    PjPresetCharacter c;
    pj_preset_character(camera, &c);
    printf("%s\n%s\n", cube_path, style_path);
    if (install)
        printf("in place. One step is darktable's: lighttable > styles > import... > %s\n"
               "(darktable keeps styles in its database; the cube is already where the 3D LUT module looks).\n", style_path);
    else
        printf("to use: copy %s.cube into darktable's 3D lut root folder (preferences > processing),\n"
               "then lighttable > styles > import... > %s.dtstyle. Or rerun with --install.\n", name, name);
    printf("carried: color (3D LUT)%s%s%s; not carried: halation%s%s\n",
           c.grain > 0.0f ? ", grain" : "", c.vignette > 0.0f ? ", vignette" : "",
           c.resolve > 0.0f ? ", format softness (lowpass, sized for a 6000 px frame)" : "",
           c.instant ? ", the print's frame" : "", c.flash ? ", the direct flash" : "");
    return EXIT_SUCCESS;
}
