/*
 * Baseline JPEG encoder, band-parallel.
 *
 * The arithmetic is stb_image_write's (the AAN float DCT, its quality
 * scaling of the Annex K quantization tables, the standard Huffman
 * tables, 4:4:4 at quality > 90), so the coefficients are the ones the
 * 1.17 writer produced. What changes is the stream structure: the
 * image is cut into bands of a fixed number of MCU rows, each band is
 * entropy-coded on its own thread with its own DC predictors, and the
 * bands are joined with restart markers (DRI = MCUs per band). The band
 * height does not depend on the thread count, so the file is the same
 * bytes at any thread count. Every baseline decoder handles restarts.
 */
#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tables (Annex K, as stb lists them) ------------------------------ */

static const unsigned char zigzag[64] = {
    0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,3,8,12,17,25,30,41,43,9,11,18,
    24,31,40,44,53,10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,21,34,37,
    47,50,56,59,61,35,36,48,49,57,58,62,63 };

static const unsigned char dc_lum_nr[17] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const unsigned char dc_lum_val[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const unsigned char ac_lum_nr[17] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const unsigned char ac_lum_val[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
    0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
    0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa };
static const unsigned char dc_chr_nr[17] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const unsigned char dc_chr_val[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const unsigned char ac_chr_nr[17] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const unsigned char ac_chr_val[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
    0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
    0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa };

static const int yqt[64] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,
                            37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
static const int uvqt[64] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
                             99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
static const float aasf[8] = { 1.0f * 2.828427125f, 1.387039845f * 2.828427125f, 1.306562965f * 2.828427125f, 1.175875602f * 2.828427125f,
                               1.0f * 2.828427125f, 0.785694958f * 2.828427125f, 0.541196100f * 2.828427125f, 0.275899379f * 2.828427125f };

typedef struct { unsigned short code, len; } HCode;

/* canonical codes from the (nrcodes, values) lists, indexed by symbol */
static void build_codes(HCode out[256], const unsigned char *nr, const unsigned char *val)
{
    memset(out, 0, 256 * sizeof *out);
    unsigned code = 0, k = 0;
    for (int l = 1; l <= 16; ++l) {
        for (int i = 0; i < nr[l]; ++i, ++k, ++code) {
            out[val[k]].code = (unsigned short)code;
            out[val[k]].len = (unsigned short)l;
        }
        code <<= 1;
    }
}

/* ---- per-band bit writer ---------------------------------------------- */

typedef struct {
    unsigned char *buf;
    size_t len, cap;
    unsigned bitbuf;
    int bitcnt;
    int failed;
} BitOut;

static void put_byte(BitOut *o, unsigned char c)
{
    if (o->len == o->cap) {
        size_t next = o->cap ? o->cap * 2 : 65536;
        unsigned char *grown = realloc(o->buf, next);
        if (!grown) { o->failed = 1; return; }
        o->buf = grown;
        o->cap = next;
    }
    o->buf[o->len++] = c;
}

static void put_bits(BitOut *o, unsigned code, int len)
{
    o->bitcnt += len;
    o->bitbuf |= code << (24 - o->bitcnt);
    while (o->bitcnt >= 8) {
        unsigned char c = (unsigned char)((o->bitbuf >> 16) & 255);
        put_byte(o, c);
        if (c == 255) put_byte(o, 0);
        o->bitbuf <<= 8;
        o->bitcnt -= 8;
    }
}

/* ---- the DCT and one data unit (stb's arithmetic) ---------------------- */

static void dct8(float *d0p, float *d1p, float *d2p, float *d3p, float *d4p, float *d5p, float *d6p, float *d7p)
{
    float d0 = *d0p, d1 = *d1p, d2 = *d2p, d3 = *d3p, d4 = *d4p, d5 = *d5p, d6 = *d6p, d7 = *d7p;
    float z1, z2, z3, z4, z5, z11, z13;
    float tmp0 = d0 + d7, tmp7 = d0 - d7, tmp1 = d1 + d6, tmp6 = d1 - d6;
    float tmp2 = d2 + d5, tmp5 = d2 - d5, tmp3 = d3 + d4, tmp4 = d3 - d4;
    float tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3, tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
    d0 = tmp10 + tmp11;
    d4 = tmp10 - tmp11;
    z1 = (tmp12 + tmp13) * 0.707106781f;
    d2 = tmp13 + z1;
    d6 = tmp13 - z1;
    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;
    z5 = (tmp10 - tmp12) * 0.382683433f;
    z2 = tmp10 * 0.541196100f + z5;
    z4 = tmp12 * 1.306562965f + z5;
    z3 = tmp11 * 0.707106781f;
    z11 = tmp7 + z3;
    z13 = tmp7 - z3;
    *d5p = z13 + z2; *d3p = z13 - z2; *d1p = z11 + z4; *d7p = z11 - z4;
    *d0p = d0; *d2p = d2; *d4p = d4; *d6p = d6;
}

static void calc_bits(int val, unsigned short bits[2])
{
    int tmp1 = val < 0 ? -val : val;
    val = val < 0 ? val - 1 : val;
    bits[1] = 1;
    while (tmp1 >>= 1) ++bits[1];
    bits[0] = (unsigned short)(val & ((1 << bits[1]) - 1));
}

static int process_du(BitOut *o, float *cdu, const float *fdtbl, int dc,
                      const HCode *htdc, const HCode *htac)
{
    int du[64];
    for (int off = 0; off < 64; off += 8)
        dct8(&cdu[off], &cdu[off + 1], &cdu[off + 2], &cdu[off + 3], &cdu[off + 4], &cdu[off + 5], &cdu[off + 6], &cdu[off + 7]);
    for (int off = 0; off < 8; ++off)
        dct8(&cdu[off], &cdu[off + 8], &cdu[off + 16], &cdu[off + 24], &cdu[off + 32], &cdu[off + 40], &cdu[off + 48], &cdu[off + 56]);
    for (int j = 0; j < 64; ++j) {
        float v = cdu[j] * fdtbl[j];
        du[zigzag[j]] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    }
    int diff = du[0] - dc;
    if (diff == 0) put_bits(o, htdc[0].code, htdc[0].len);
    else {
        unsigned short bits[2];
        calc_bits(diff, bits);
        put_bits(o, htdc[bits[1]].code, htdc[bits[1]].len);
        put_bits(o, bits[0], bits[1]);
    }
    int end0pos = 63;
    while (end0pos > 0 && du[end0pos] == 0) --end0pos;
    if (end0pos == 0) {
        put_bits(o, htac[0].code, htac[0].len);
        return du[0];
    }
    for (int i = 1; i <= end0pos; ++i) {
        int startpos = i;
        while (du[i] == 0 && i <= end0pos) ++i;
        int nrzeroes = i - startpos;
        if (nrzeroes >= 16) {
            int lng = nrzeroes >> 4;
            for (int m = 1; m <= lng; ++m) put_bits(o, htac[0xF0].code, htac[0xF0].len);
            nrzeroes &= 15;
        }
        unsigned short bits[2];
        calc_bits(du[i], bits);
        int sym = (nrzeroes << 4) + bits[1];
        put_bits(o, htac[sym].code, htac[sym].len);
        put_bits(o, bits[0], bits[1]);
    }
    if (end0pos != 63) put_bits(o, htac[0].code, htac[0].len);
    return du[0];
}

/* ---- bands ------------------------------------------------------------ */

#define MCU_ROWS_PER_BAND 8   /* 64 pixel rows; fixed, so the file does
                                 not depend on the thread count */

typedef struct {
    const unsigned char *rgb;
    int width, height;
    int mcus_x, mcu_rows, bands;
    float fdtbl_y[64], fdtbl_uv[64];
    HCode ydc[256], yac[256], uvdc[256], uvac[256];
    BitOut *out;      /* one per band */
} EncCtx;

static void encode_band(size_t b0, size_t b1, void *vctx)
{
    EncCtx *e = vctx;
    for (size_t b = b0; b < b1; ++b) {
        BitOut *o = &e->out[b];
        int dcy = 0, dcu = 0, dcv = 0;
        int row0 = (int)b * MCU_ROWS_PER_BAND;
        int row1 = row0 + MCU_ROWS_PER_BAND;
        if (row1 > e->mcu_rows) row1 = e->mcu_rows;
        for (int my = row0; my < row1; ++my)
            for (int mx = 0; mx < e->mcus_x; ++mx) {
                float Y[64], U[64], V[64];
                int pos = 0;
                for (int row = my * 8; row < my * 8 + 8; ++row) {
                    int cr = row < e->height ? row : e->height - 1;
                    const unsigned char *line = e->rgb + (size_t)cr * e->width * 3;
                    for (int col = mx * 8; col < mx * 8 + 8; ++col, ++pos) {
                        int cc = col < e->width ? col : e->width - 1;
                        float r = line[cc * 3], g = line[cc * 3 + 1], bb = line[cc * 3 + 2];
                        Y[pos] = +0.29900f * r + 0.58700f * g + 0.11400f * bb - 128;
                        U[pos] = -0.16874f * r - 0.33126f * g + 0.50000f * bb;
                        V[pos] = +0.50000f * r - 0.41869f * g - 0.08131f * bb;
                    }
                }
                dcy = process_du(o, Y, e->fdtbl_y, dcy, e->ydc, e->yac);
                dcu = process_du(o, U, e->fdtbl_uv, dcu, e->uvdc, e->uvac);
                dcv = process_du(o, V, e->fdtbl_uv, dcv, e->uvdc, e->uvac);
            }
        /* byte-align with 1-bits, as before a marker */
        put_bits(o, 0x7F, 7);
    }
}

bool pj_jpeg_write(const char *path, const unsigned char *rgb, size_t width,
                   size_t height, int quality, const char *comment,
                   PjError *error)
{
    if (width == 0 || height == 0 || width > 65535 || height > 65535) {
        pj_set_error(error, "JPEG: dimensions out of range");
        return false;
    }
    EncCtx e;
    memset(&e, 0, sizeof e);
    e.rgb = rgb;
    e.width = (int)width;
    e.height = (int)height;
    e.mcus_x = (e.width + 7) / 8;
    e.mcu_rows = (e.height + 7) / 8;
    e.bands = (e.mcu_rows + MCU_ROWS_PER_BAND - 1) / MCU_ROWS_PER_BAND;

    quality = quality ? quality : 90;
    quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    int q = quality < 50 ? 5000 / quality : 200 - quality * 2;
    unsigned char ytable[64], uvtable[64];
    for (int i = 0; i < 64; ++i) {
        int yti = (yqt[i] * q + 50) / 100, uvti = (uvqt[i] * q + 50) / 100;
        ytable[zigzag[i]] = (unsigned char)(yti < 1 ? 1 : yti > 255 ? 255 : yti);
        uvtable[zigzag[i]] = (unsigned char)(uvti < 1 ? 1 : uvti > 255 ? 255 : uvti);
    }
    for (int row = 0, k = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col, ++k) {
            e.fdtbl_y[k] = 1 / (ytable[zigzag[k]] * aasf[row] * aasf[col]);
            e.fdtbl_uv[k] = 1 / (uvtable[zigzag[k]] * aasf[row] * aasf[col]);
        }
    build_codes(e.ydc, dc_lum_nr, dc_lum_val);
    build_codes(e.yac, ac_lum_nr, ac_lum_val);
    build_codes(e.uvdc, dc_chr_nr, dc_chr_val);
    build_codes(e.uvac, ac_chr_nr, ac_chr_val);

    e.out = calloc((size_t)e.bands, sizeof *e.out);
    if (!e.out) { pj_set_error(error, "out of memory"); return false; }
    pj_parallel_rows((size_t)e.bands, encode_band, &e);
    for (int b = 0; b < e.bands; ++b)
        if (e.out[b].failed) {
            for (int k = 0; k < e.bands; ++k) free(e.out[k].buf);
            free(e.out);
            pj_set_error(error, "out of memory");
            return false;
        }

    FILE *f = fopen(path, "wb");
    if (!f) {
        for (int k = 0; k < e.bands; ++k) free(e.out[k].buf);
        free(e.out);
        pj_set_error(error, "cannot write '%s'", path);
        return false;
    }
    bool ok = true;
    {
        static const unsigned char head0[] = { 0xFF,0xD8,0xFF,0xE0,0,0x10,'J','F','I','F',0,1,1,0,0,1,0,1,0,0,0xFF,0xDB,0,0x84,0 };
        unsigned char head1[] = { 0xFF,0xC0,0,0x11,8,(unsigned char)(e.height >> 8),(unsigned char)e.height,
                                  (unsigned char)(e.width >> 8),(unsigned char)e.width,
                                  3,1,0x11,0,2,0x11,1,3,0x11,1,0xFF,0xC4,0x01,0xA2,0 };
        static const unsigned char head2[] = { 0xFF,0xDA,0,0xC,3,1,0,2,0x11,3,0x11,0,0x3F,0 };
        unsigned restart = (unsigned)e.mcus_x * MCU_ROWS_PER_BAND;
        unsigned char dri[] = { 0xFF,0xDD,0,4,(unsigned char)(restart >> 8),(unsigned char)restart };
        unsigned char one = 1, ac0 = 0x10, ac1 = 0x11;
        /* SOI + APP0 first, then the comment, then the tables */
        ok = ok && fwrite(head0, 1, 20, f) == 20;
        if (comment && *comment) {
            size_t clen = strlen(comment);
            if (clen > 65533) clen = 65533;
            unsigned char com[4] = { 0xFF, 0xFE, (unsigned char)((clen + 2) >> 8),
                                     (unsigned char)((clen + 2) & 255) };
            ok = ok && fwrite(com, 1, 4, f) == 4;
            ok = ok && fwrite(comment, 1, clen, f) == clen;
        }
        ok = ok && fwrite(head0 + 20, 1, sizeof head0 - 20, f) == sizeof head0 - 20;
        ok = ok && fwrite(ytable, 1, 64, f) == 64;
        ok = ok && fwrite(&one, 1, 1, f) == 1;
        ok = ok && fwrite(uvtable, 1, 64, f) == 64;
        ok = ok && fwrite(head1, 1, sizeof head1, f) == sizeof head1;
        ok = ok && fwrite(dc_lum_nr + 1, 1, 16, f) == 16;
        ok = ok && fwrite(dc_lum_val, 1, sizeof dc_lum_val, f) == sizeof dc_lum_val;
        ok = ok && fwrite(&ac0, 1, 1, f) == 1;
        ok = ok && fwrite(ac_lum_nr + 1, 1, 16, f) == 16;
        ok = ok && fwrite(ac_lum_val, 1, sizeof ac_lum_val, f) == sizeof ac_lum_val;
        ok = ok && fwrite(&one, 1, 1, f) == 1;
        ok = ok && fwrite(dc_chr_nr + 1, 1, 16, f) == 16;
        ok = ok && fwrite(dc_chr_val, 1, sizeof dc_chr_val, f) == sizeof dc_chr_val;
        ok = ok && fwrite(&ac1, 1, 1, f) == 1;
        ok = ok && fwrite(ac_chr_nr + 1, 1, 16, f) == 16;
        ok = ok && fwrite(ac_chr_val, 1, sizeof ac_chr_val, f) == sizeof ac_chr_val;
        if (e.bands > 1) ok = ok && fwrite(dri, 1, sizeof dri, f) == sizeof dri;
        ok = ok && fwrite(head2, 1, sizeof head2, f) == sizeof head2;
        for (int b = 0; b < e.bands && ok; ++b) {
            if (b > 0) {
                unsigned char rst[2] = { 0xFF, (unsigned char)(0xD0 + ((b - 1) & 7)) };
                ok = fwrite(rst, 1, 2, f) == 2;
            }
            ok = ok && fwrite(e.out[b].buf, 1, e.out[b].len, f) == e.out[b].len;
        }
        static const unsigned char eoi[] = { 0xFF, 0xD9 };
        ok = ok && fwrite(eoi, 1, 2, f) == 2;
    }
    if (fclose(f) != 0) ok = false;
    for (int k = 0; k < e.bands; ++k) free(e.out[k].buf);
    free(e.out);
    if (!ok) pj_set_error(error, "cannot write '%s'", path);
    return ok;
}
