/*
 * Row-band parallelism for the pipeline. Every image stage is a loop
 * over rows (or columns, for the vertical blur pass) whose iterations
 * are independent, so each stage hands a band function to
 * pj_parallel_rows() and the bands run on one thread per core.
 *
 * Determinism: a band computes exactly what the serial loop computed
 * for those rows, in the same order, so output is bit-identical at any
 * thread count. Stages that reduce (a sum over the whole frame) stay
 * serial.
 *
 * POSIX threads, part of every libc the project builds on; the wasm
 * build (no threads in WASI) and a build with -DPJ_NO_THREADS run the
 * bands serially through the same entry point. POLAJUICE_THREADS=N
 * overrides the core count (1 = serial), for benchmarks and bisection.
 */
#define _POSIX_C_SOURCE 200809L

#include "internal.h"

#include <stdlib.h>

#if defined(__wasi__) || defined(PJ_NO_THREADS)
#define PJ_SERIAL 1
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define PJ_MAX_THREADS 32

typedef struct {
    PjRowFn fn;
    void *ctx;
    size_t y0, y1;
} Band;

#ifndef PJ_SERIAL
static void *band_main(void *arg)
{
    Band *b = arg;
    b->fn(b->y0, b->y1, b->ctx);
    return NULL;
}
#endif

static unsigned thread_count(void)
{
    static int cached = -1;
    if (cached >= 0) return (unsigned)cached;
    long n = 1;
    const char *env = getenv("POLAJUICE_THREADS");
    if (env && *env) n = strtol(env, NULL, 10);
#ifndef PJ_SERIAL
    else {
        n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n < 1) n = 1;
    }
#endif
    if (n < 1) n = 1;
    if (n > PJ_MAX_THREADS) n = PJ_MAX_THREADS;
    cached = (int)n;
    return (unsigned)n;
}

unsigned pj_thread_count(void) { return thread_count(); }

void pj_parallel_rows(size_t rows, PjRowFn fn, void *ctx)
{
    if (rows == 0) return;
    unsigned n = thread_count();
    /* a few rows per thread is not worth the spawn */
    if (rows < 64) n = 1;
    if (n > rows) n = (unsigned)rows;
#ifdef PJ_SERIAL
    n = 1;
#endif
    if (n == 1) { fn(0, rows, ctx); return; }
#ifndef PJ_SERIAL
    Band bands[PJ_MAX_THREADS];
    pthread_t threads[PJ_MAX_THREADS];
    size_t per = rows / n, extra = rows % n, y = 0;
    unsigned started = 0;
    for (unsigned t = 0; t < n; ++t) {
        size_t len = per + (t < extra ? 1 : 0);
        bands[t] = (Band){fn, ctx, y, y + len};
        y += len;
        if (t == n - 1) break;                  /* last band on this thread */
        if (pthread_create(&threads[t], NULL, band_main, &bands[t]) != 0) {
            /* spawn failed: run the rest serially, in order */
            for (unsigned u = t; u < n; ++u) {
                size_t l = per + (u < extra ? 1 : 0);
                if (u > t) { bands[u] = (Band){fn, ctx, y, y + l}; y += l; }
                fn(bands[u].y0, bands[u].y1, ctx);
            }
            for (unsigned u = 0; u < started; ++u) pthread_join(threads[u], NULL);
            return;
        }
        ++started;
    }
    fn(bands[n - 1].y0, bands[n - 1].y1, ctx);
    for (unsigned t = 0; t < started; ++t) pthread_join(threads[t], NULL);
#endif
}
