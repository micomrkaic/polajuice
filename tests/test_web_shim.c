/* Exercises the WASM shim natively: enumeration, both film modes, preview
 * downscale, and error paths. Same code emcc compiles for the browser. */
#include "polajuice.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

const char *pj_web_status(void);
const char *pj_web_version(void);
int pj_web_camera_count(void);
const char *pj_web_camera_name(int index);
const char *pj_web_camera_film(int index);
int pj_web_render(const char *input_path, const char *camera, int use_film,
                  float strength, double seed, int max_dim);

int main(void)
{
    assert(!strcmp(pj_web_version(), pj_version_string()));
    assert(pj_web_camera_count() >= 9);
    int polaroid = -1;
    for (int i = 0; i < pj_web_camera_count(); ++i)
        if (!strcmp(pj_web_camera_name(i), "polaroid-600")) polaroid = i;
    assert(polaroid >= 0);
    assert(!strcmp(pj_web_camera_film(polaroid), "polaroid_px-680"));

    /* Unique per-run input name: fixed /tmp names collide between users
     * on shared machines (found the hard way). */
    char in_path[128];
    snprintf(in_path, sizeof in_path, "/tmp/pj_shim_in_%ld.png", (long)getpid());
    PjError e = {{0}};
    PjImage *img = pj_image_new(320, 240, &e);
    assert(img);
    float *px = pj_image_pixels(img);
    for (size_t i = 0; i < 320 * 240 * 3; ++i) px[i] = (float)(i % 97) / 96.0f;
    assert(pj_image_save(img, in_path, &e));
    pj_image_free(img);

    /* film mode via /film.cube (identity), full size */
    /* must match the shim's native per-uid PJ_WEB_FILM */
    char film_path[64], out_path[64];
    snprintf(film_path, sizeof film_path, "/tmp/pj_web_film_%ld.cube", (long)getuid());
    snprintf(out_path, sizeof out_path, "/tmp/pj_web_out_%ld.jpg", (long)getuid());
    FILE *f = fopen(film_path, "w");
    assert(f);
    fputs("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
          "0 0 1\n1 0 1\n0 1 1\n1 1 1\n", f);
    fclose(f);
    int size_full = pj_web_render(in_path, "polaroid-600", 1, 1.0f, 42, 0);
    assert(size_full > 0);

    /* scalar mode; downscale is checked on a frameless camera because the
     * instant frame legitimately expands the canvas past max_dim */
    int size_prev = pj_web_render(in_path, "35mm-negative", 0, 1.0f, 42, 96);
    assert(size_prev > 0 && size_prev < size_full);
    PjImage *out = pj_image_load(out_path, &e);
    assert(out);
    assert(pj_image_width(out) <= 96 && pj_image_height(out) <= 96);
    pj_image_free(out);

    /* error paths report, not crash */
    assert(pj_web_render("/tmp/in.png", "no-such-camera", 0, 1.0f, 1, 0) == 0);
    assert(pj_web_status()[0] != '\0');
    assert(pj_web_render("/tmp/missing.png", "polaroid-600", 0, 1.0f, 1, 0) == 0);

    remove(in_path);
    puts("web shim tests passed");
    return 0;
}
