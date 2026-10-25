/*
 * Raw front door: scene-linear input from camera files.
 *
 * Two containers, both TIFF-based, one walker:
 *
 *   DNG  - Adobe's documented format: CFA (Bayer) or LinearRaw (already
 *          demosaiced, e.g. iPhone ProRAW) image data, uncompressed or
 *          lossless JPEG (ITU T.81 process 14), with the sensor's black
 *          and white levels, the as-shot white balance and the XYZ->camera
 *          color matrix carried in the file.
 *   NEF  - Nikon's TIFF: the raw IFD is Bayer data, either uncompressed
 *          or in Nikon's Huffman scheme (compression 34713) whose trees,
 *          predictors and linearization curve live in the MakerNote.
 *          Nothing in the file says how camera RGB maps to XYZ, so a
 *          small per-model matrix table lives here (see
 *          docs/PRESET_SOURCES.md for provenance).
 *
 * Output is scene-referred linear sRGB-primaries RGB with the sensor's
 * white at 1.0 and nothing clipped above it: the pipeline opens raw
 * images one stop up and rolls the headroom in with a shoulder, which
 * is what lets halation and the film curve see real highlights instead
 * of a plateau at 1.0.
 *
 * A --linear 16-bit TIFF or PNG (dcraw -4 -T, darktable's linear export)
 * takes the same door without the demosaic: values/65535 are taken as
 * scene-linear with no gamma decode.
 *
 * Dependency-free C17 throughout, like the rest of polajuice.
 */
#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

/* ---- byte reader ------------------------------------------------------ */

typedef struct {
    const unsigned char *data;
    size_t size;
    bool big_endian;
} Bytes;

static bool in_range(const Bytes *b, size_t off, size_t n)
{
    return off <= b->size && n <= b->size - off;
}

static unsigned get16(const Bytes *b, size_t off)
{
    if (!in_range(b, off, 2)) return 0;
    const unsigned char *p = b->data + off;
    return b->big_endian ? (unsigned)(p[0] << 8 | p[1])
                         : (unsigned)(p[1] << 8 | p[0]);
}

static uint32_t get32(const Bytes *b, size_t off)
{
    if (!in_range(b, off, 4)) return 0;
    const unsigned char *p = b->data + off;
    return b->big_endian
        ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]
        : (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

/* ---- TIFF directory entries ------------------------------------------- */

typedef struct {
    unsigned tag, type;
    uint32_t count;
    size_t value_off;   /* where the value bytes live (inline or pointed) */
} Entry;

static const size_t type_size[13] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};

/* Reads entry i of the IFD at off (relative to the TIFF header base). */
static bool read_entry(const Bytes *b, size_t base, size_t ifd, unsigned i,
                       Entry *e)
{
    size_t at = base + ifd + 2 + (size_t)i * 12;
    if (!in_range(b, at, 12)) return false;
    e->tag = get16(b, at);
    e->type = get16(b, at + 2);
    e->count = get32(b, at + 4);
    size_t ts = e->type < 13 ? type_size[e->type] : 0;
    if (ts == 0) return false;
    if (e->count > SIZE_MAX / ts) return false;
    size_t bytes = ts * e->count;
    e->value_off = bytes <= 4 ? at + 8 : base + get32(b, at + 8);
    return in_range(b, e->value_off, bytes);
}

static double entry_num(const Bytes *b, const Entry *e, uint32_t i)
{
    if (i >= e->count) return 0.0;
    size_t off = e->value_off + (size_t)i * type_size[e->type];
    switch (e->type) {
    case 1: case 7: return in_range(b, off, 1) ? b->data[off] : 0.0;
    case 3: return get16(b, off);
    case 4: return get32(b, off);
    case 5: { double d = get32(b, off + 4);
              return d != 0.0 ? get32(b, off) / d : 0.0; }
    case 6: return in_range(b, off, 1) ? (signed char)b->data[off] : 0.0;
    case 8: return (int16_t)get16(b, off);
    case 9: return (int32_t)get32(b, off);
    case 10: { double d = (int32_t)get32(b, off + 4);
               return d != 0.0 ? (int32_t)get32(b, off) / d : 0.0; }
    case 11: { uint32_t u = get32(b, off); float f; memcpy(&f, &u, 4); return f; }
    case 12: { uint64_t u = (uint64_t)get32(b, off + (b->big_endian ? 0 : 4)) << 32
                          | get32(b, off + (b->big_endian ? 4 : 0));
               double d; memcpy(&d, &u, 8); return d; }
    default: return 0.0;
    }
}

/* ---- what one image directory tells us ------------------------------- */

typedef struct {
    size_t ifd;             /* offset of the IFD (relative to base) */
    uint32_t subfile;
    size_t width, height;
    unsigned bps, compression, photometric, spp;
    unsigned orientation;
    size_t tile_w, tile_h;
    Entry strip_offsets, strip_counts, rows_per_strip;
    Entry tile_offsets, tile_counts;
    Entry cfa_pattern, cfa_dim;
    Entry lin_table, black, black_dim, white;
    Entry cm1, cm2, as_shot, illum1, illum2;
    Entry active_area, crop_origin, crop_size;
    bool have_cfa_pattern, have_black, have_white, have_lin;
    bool have_cm1, have_cm2, have_as_shot, have_active, have_crop_origin, have_crop_size;
} Ifd;

typedef struct {
    Bytes b;
    size_t base;            /* TIFF header offset in the file */
    Ifd raw;                /* the directory we decode */
    bool have_raw;
    bool is_dng;
    char make[64], model[64];
    size_t exif_ifd, makernote_off, makernote_len;
    unsigned orientation0;
    Entry cm1_0, cm2_0, as_shot_0, illum1_0, illum2_0;
    bool have_cm1_0, have_cm2_0, have_as_shot_0;
} Tiff;

static void read_string(const Bytes *b, const Entry *e, char *out, size_t n)
{
    size_t len = e->count < n - 1 ? e->count : n - 1;
    if (!in_range(b, e->value_off, len)) { out[0] = 0; return; }
    memcpy(out, b->data + e->value_off, len);
    out[len] = 0;
    while (len && (out[len - 1] == ' ' || out[len - 1] == 0)) out[--len] = 0;
}

static void parse_ifd(Tiff *t, size_t ifd, int depth);

static void parse_image_ifd(Tiff *t, size_t ifd, int depth)
{
    const Bytes *b = &t->b;
    Ifd d = {0};
    d.ifd = ifd;
    d.spp = 1; d.bps = 16; d.compression = 1; d.orientation = 1;
    unsigned n = get16(b, t->base + ifd);
    if (n == 0 || n > 512) return;
    Entry e;
    for (unsigned i = 0; i < n; ++i) {
        if (!read_entry(b, t->base, ifd, i, &e)) continue;
        switch (e.tag) {
        case 254: d.subfile = (uint32_t)entry_num(b, &e, 0); break;
        case 256: d.width = (size_t)entry_num(b, &e, 0); break;
        case 257: d.height = (size_t)entry_num(b, &e, 0); break;
        case 258: d.bps = (unsigned)entry_num(b, &e, 0); break;
        case 259: d.compression = (unsigned)entry_num(b, &e, 0); break;
        case 262: d.photometric = (unsigned)entry_num(b, &e, 0); break;
        case 271: read_string(b, &e, t->make, sizeof t->make); break;
        case 272: read_string(b, &e, t->model, sizeof t->model); break;
        case 273: d.strip_offsets = e; break;
        case 274:
            d.orientation = (unsigned)entry_num(b, &e, 0);
            if (depth == 0) t->orientation0 = d.orientation;
            break;
        case 277: d.spp = (unsigned)entry_num(b, &e, 0); break;
        case 278: d.rows_per_strip = e; break;
        case 279: d.strip_counts = e; break;
        case 322: d.tile_w = (size_t)entry_num(b, &e, 0); break;
        case 323: d.tile_h = (size_t)entry_num(b, &e, 0); break;
        case 324: d.tile_offsets = e; break;
        case 325: d.tile_counts = e; break;
        case 330:   /* SubIFDs */
            for (uint32_t k = 0; k < e.count && k < 16; ++k)
                parse_ifd(t, (size_t)entry_num(b, &e, k), depth + 1);
            break;
        case 33421: d.cfa_dim = e; break;
        case 33422: d.cfa_pattern = e; d.have_cfa_pattern = true; break;
        case 34665: t->exif_ifd = (size_t)entry_num(b, &e, 0); break;
        case 37500:  /* MakerNote, when it sits in IFD0 */
            t->makernote_off = e.value_off; t->makernote_len = e.count; break;
        case 50706: t->is_dng = true; break;
        case 50712: d.lin_table = e; d.have_lin = true; break;
        case 50713: d.black_dim = e; break;
        case 50714: d.black = e; d.have_black = true; break;
        case 50717: d.white = e; d.have_white = true; break;
        case 50721: if (depth == 0) { t->cm1_0 = e; t->have_cm1_0 = true; }
                    d.cm1 = e; d.have_cm1 = true; break;
        case 50722: if (depth == 0) { t->cm2_0 = e; t->have_cm2_0 = true; }
                    d.cm2 = e; d.have_cm2 = true; break;
        case 50728: if (depth == 0) { t->as_shot_0 = e; t->have_as_shot_0 = true; }
                    d.as_shot = e; d.have_as_shot = true; break;
        case 50778: if (depth == 0) t->illum1_0 = e; d.illum1 = e; break;
        case 50779: if (depth == 0) t->illum2_0 = e; d.illum2 = e; break;
        case 50719: d.crop_origin = e; d.have_crop_origin = true; break;
        case 50720: d.crop_size = e; d.have_crop_size = true; break;
        case 50829: d.active_area = e; d.have_active = true; break;
        default: break;
        }
    }
    /* the raw image is the largest CFA / LinearRaw directory, or for
     * Nikon the largest Nikon-compressed one */
    bool raw_like = d.photometric == 32803 || d.photometric == 34892 ||
                    d.compression == 34713;
    if (raw_like && d.width && d.height &&
        (!t->have_raw || d.width * d.height > t->raw.width * t->raw.height)) {
        t->raw = d;
        t->have_raw = true;
    }
}

static void parse_ifd(Tiff *t, size_t ifd, int depth)
{
    if (depth > 4) return;
    for (int chain = 0; chain < 8 && ifd && in_range(&t->b, t->base + ifd, 2); ++chain) {
        parse_image_ifd(t, ifd, depth);
        unsigned n = get16(&t->b, t->base + ifd);
        ifd = get32(&t->b, t->base + ifd + 2 + (size_t)n * 12);
    }
}

static bool parse_tiff(Tiff *t, PjError *error)
{
    const Bytes *b = &t->b;
    if (b->size < 8) { pj_set_error(error, "raw file too small"); return false; }
    if (b->data[0] == 'I' && b->data[1] == 'I') t->b.big_endian = false;
    else if (b->data[0] == 'M' && b->data[1] == 'M') t->b.big_endian = true;
    else { pj_set_error(error, "not a TIFF-based raw file"); return false; }
    if (get16(b, 2) != 42) { pj_set_error(error, "not a TIFF-based raw file"); return false; }
    t->base = 0;
    parse_ifd(t, get32(b, 4), 0);
    if (t->exif_ifd) {
        size_t ifd = t->exif_ifd;
        unsigned n = get16(b, t->base + ifd);
        Entry e;
        for (unsigned i = 0; i < n && n < 512; ++i) {
            if (!read_entry(b, t->base, ifd, i, &e)) continue;
            if (e.tag == 37500) { t->makernote_off = e.value_off; t->makernote_len = e.count; }
        }
    }
    if (!t->have_raw) {
        pj_set_error(error, "no raw image directory found (CFA or LinearRaw)");
        return false;
    }
    return true;
}

/* ---- bit reader and Huffman ------------------------------------------- */

typedef struct {
    const unsigned char *p, *end;
    uint32_t acc;
    int nbits;
    bool zero_after_ff;   /* JPEG byte stuffing: FF 00 is a literal FF */
    int padded;           /* zero bits appended past the data (or past a
                             marker); consuming into them is an overrun */
    bool overrun;
} BitReader;

/* make at least n bits available in acc */
static void fillbits(BitReader *r, int n)
{
    while (r->nbits < n) {
        unsigned c = 0;
        if (r->p < r->end) {
            c = *r->p++;
            if (r->zero_after_ff && c == 0xFF) {
                if (r->p < r->end && *r->p == 0x00) ++r->p;
                else { --r->p; c = 0; r->padded += 8; }   /* a marker: data ended */
            }
        } else r->padded += 8;
        r->acc = r->acc << 8 | c;
        r->nbits += 8;
    }
}

static unsigned getbits(BitReader *r, int n)
{
    if (n <= 0) return 0;
    fillbits(r, n);
    unsigned v = (r->acc >> (r->nbits - n)) & ((1u << n) - 1u);
    r->nbits -= n;
    if (r->nbits < r->padded) r->overrun = true;
    return v;
}

static unsigned peekbits(BitReader *r, int n)
{
    fillbits(r, n);
    return (r->acc >> (r->nbits - n)) & ((1u << n) - 1u);
}

/* Canonical Huffman table from JPEG-style (16 counts, then symbols).
 * A 9-bit first-level lookup resolves nearly every code in one step
 * (raw difference categories are short); longer codes fall back to the
 * canonical walk. */
#define HUFF_LOOKUP_BITS 9
typedef struct {
    unsigned char symbol[256];
    unsigned count[17];
    unsigned mincode[17], maxcode[17], first[17];
    unsigned char lookup_sym[1 << HUFF_LOOKUP_BITS];
    unsigned char lookup_len[1 << HUFF_LOOKUP_BITS];   /* 0 = not resolved */
    int ok;
} Huff;

static void huff_build(Huff *h, const unsigned char *counts, const unsigned char *symbols)
{
    memset(h, 0, sizeof *h);
    unsigned total = 0, code = 0;
    for (int l = 1; l <= 16; ++l) {
        h->count[l] = counts[l - 1];
        h->first[l] = total;
        h->mincode[l] = code;
        h->maxcode[l] = code + h->count[l];
        code = (code + h->count[l]) << 1;
        total += h->count[l];
    }
    if (total > 256) total = 256;
    memcpy(h->symbol, symbols, total);
    /* fill the lookup: every code of length l <= LOOKUP_BITS owns the
     * 2^(LOOKUP_BITS-l) slots that start with it */
    unsigned k = 0;
    code = 0;
    for (int l = 1; l <= 16; ++l) {
        for (unsigned i = 0; i < h->count[l]; ++i, ++k, ++code) {
            if (l <= HUFF_LOOKUP_BITS && k < 256) {
                unsigned base = code << (HUFF_LOOKUP_BITS - l);
                unsigned span = 1u << (HUFF_LOOKUP_BITS - l);
                for (unsigned j = 0; j < span; ++j) {
                    h->lookup_sym[base + j] = h->symbol[k];
                    h->lookup_len[base + j] = (unsigned char)l;
                }
            }
        }
        code <<= 1;
    }
    h->ok = 1;
}

static int huff_decode(BitReader *r, const Huff *h)
{
    unsigned peek = peekbits(r, HUFF_LOOKUP_BITS);
    unsigned len = h->lookup_len[peek];
    if (len) {
        r->nbits -= (int)len;
        if (r->nbits < r->padded) r->overrun = true;
        return h->lookup_sym[peek];
    }
    unsigned code = 0;
    for (int l = 1; l <= 16; ++l) {
        code = code << 1 | getbits(r, 1);
        if (h->count[l] && code < h->maxcode[l] && code >= h->mincode[l])
            return h->symbol[h->first[l] + code - h->mincode[l]];
    }
    r->overrun = true;
    return 0;
}

/* JPEG DC difference: a length then that many magnitude bits */
static int ljpeg_diff(BitReader *r, const Huff *h)
{
    int len = huff_decode(r, h);
    if (len == 16) return -32768;
    if (len == 0) return 0;
    int diff = (int)getbits(r, len);
    if ((diff & (1 << (len - 1))) == 0) diff -= (1 << len) - 1;
    return diff;
}

/* ---- lossless JPEG (T.81 process 14) --------------------------------- */

typedef struct {
    unsigned wide, high, clrs, bits, pred, point;
    Huff huff[4];
    const unsigned char *scan;   /* entropy data start */
    size_t scan_len;
    unsigned restart;
} LJpeg;

static bool ljpeg_header(LJpeg *j, const unsigned char *p, size_t n)
{
    memset(j, 0, sizeof *j);
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;
    size_t off = 2;
    unsigned comp_table[4] = {0};
    while (off + 4 <= n) {
        if (p[off] != 0xFF) { ++off; continue; }
        unsigned marker = p[off + 1];
        if (marker == 0xD8 || marker == 0xFF) { off += (marker == 0xD8) ? 2 : 1; continue; }
        unsigned len = p[off + 2] << 8 | p[off + 3];
        if (len < 2 || off + 2 + len > n) return false;
        const unsigned char *s = p + off + 4;
        unsigned slen = len - 2;
        switch (marker) {
        case 0xC3:  /* SOF3 */
            if (slen < 6) return false;
            j->bits = s[0];
            j->high = s[1] << 8 | s[2];
            j->wide = s[3] << 8 | s[4];
            j->clrs = s[5];
            if (j->clrs > 4 || slen < 6 + 3 * j->clrs) return false;
            for (unsigned c = 0; c < j->clrs; ++c) comp_table[c] = 0;
            break;
        case 0xC4: {  /* DHT, possibly several tables */
            unsigned k = 0;
            while (k + 17 <= slen) {
                unsigned tc = s[k] & 0x0F;
                unsigned total = 0;
                for (int i = 0; i < 16; ++i) total += s[k + 1 + i];
                if (tc > 3 || k + 17 + total > slen) return false;
                huff_build(&j->huff[tc], s + k + 1, s + k + 17);
                k += 17 + total;
            }
            break;
        }
        case 0xDD:  /* DRI */
            if (slen >= 2) j->restart = s[0] << 8 | s[1];
            break;
        case 0xDA: {  /* SOS */
            if (slen < 1) return false;
            unsigned ns = s[0];
            if (ns != j->clrs || slen < 1 + 2 * ns + 3) return false;
            for (unsigned c = 0; c < ns; ++c) comp_table[c] = s[2 + 2 * c] >> 4;
            j->pred = s[1 + 2 * ns];
            j->point = s[3 + 2 * ns] & 0x0F;
            /* remap: huff[c] must be the table for component c */
            Huff tmp[4];
            for (unsigned c = 0; c < ns; ++c) tmp[c] = j->huff[comp_table[c] & 3];
            for (unsigned c = 0; c < ns; ++c) j->huff[c] = tmp[c];
            j->scan = p + off + 2 + len;
            j->scan_len = n - (off + 2 + len);
            return j->bits >= 2 && j->bits <= 16 && j->wide && j->high;
        }
        default: break;
        }
        off += 2 + len;
    }
    return false;
}

/* Decodes the whole scan into out (high rows of wide*clrs samples). */
static bool ljpeg_decode(const LJpeg *j, uint16_t *out)
{
    BitReader r = {j->scan, j->scan + j->scan_len, 0, 0, true, 0, false};
    unsigned rowlen = j->wide * j->clrs;
    unsigned mcu = 0;
    for (unsigned row = 0; row < j->high; ++row) {
        uint16_t *cur = out + (size_t)row * rowlen;
        const uint16_t *up = row ? cur - rowlen : NULL;
        for (unsigned col = 0; col < j->wide; ++col) {
            if (j->restart && mcu && mcu % j->restart == 0) {
                /* byte-align and skip the RSTn marker */
                r.nbits = 0; r.acc = 0;
                while (r.p + 1 < r.end && !(r.p[0] == 0xFF && (r.p[1] & 0xF8) == 0xD0)) ++r.p;
                if (r.p + 1 < r.end) r.p += 2;
                up = NULL;  /* restart: first row again, predictor resets */
            }
            ++mcu;
            for (unsigned c = 0; c < j->clrs; ++c) {
                int diff = ljpeg_diff(&r, &j->huff[c]);
                int pred;
                unsigned i = col * j->clrs + c;
                if (!up) {
                    pred = col == 0 ? 1 << (j->bits - j->point - 1)
                                    : cur[i - j->clrs];
                } else if (col == 0) {
                    pred = up[i];
                } else {
                    int ra = cur[i - j->clrs], rb = up[i], rc = up[i - j->clrs];
                    switch (j->pred) {
                    case 1: pred = ra; break;
                    case 2: pred = rb; break;
                    case 3: pred = rc; break;
                    case 4: pred = ra + rb - rc; break;
                    case 5: pred = ra + ((rb - rc) >> 1); break;
                    case 6: pred = rb + ((ra - rc) >> 1); break;
                    case 7: pred = (ra + rb) >> 1; break;
                    default: pred = ra; break;
                    }
                }
                int v = pred + diff;
                cur[i] = (uint16_t)(v & 0xFFFF);
            }
        }
        if (r.overrun) return false;
    }
    return true;
}

/* ---- Nikon compressed (34713) ----------------------------------------- */

static const unsigned char nikon_tree[6][32] = {
    { 0,1,5,1,1,1,1,1,1,2,0,0,0,0,0,0,    /* 12-bit lossless */
      5,4,3,6,2,7,1,0,8,9,11,10,12 },
    { 0,1,5,1,1,1,1,1,1,2,0,0,0,0,0,0,    /* 12-bit lossy after split */
      0x39,0x5a,0x38,0x27,0x16,5,4,3,2,1,0,11,12,12 },
    { 0,1,4,2,3,1,2,0,0,0,0,0,0,0,0,0,    /* 12-bit lossy */
      5,4,6,3,7,2,8,1,9,0,10,11,12 },
    { 0,1,4,3,1,1,1,1,1,2,0,0,0,0,0,0,    /* 14-bit lossless */
      5,6,4,7,8,3,9,2,1,0,10,11,12,13,14 },
    { 0,1,5,1,1,1,1,1,1,1,2,0,0,0,0,0,    /* 14-bit lossy after split */
      8,0x5c,0x4b,0x3a,0x29,7,6,5,4,3,2,1,0,13,14 },
    { 0,1,4,2,2,3,1,2,0,0,0,0,0,0,0,0,    /* 14-bit lossy */
      7,6,8,5,9,4,10,3,11,12,2,0,1,13,14 } };

typedef struct {
    unsigned ver0, ver1;
    size_t curve_off;      /* NEFLinearizationTable (0x96) in the makernote */
    size_t curve_len;
    float wb[4];           /* R, G, B, G2 multipliers from 0x0c */
    bool have_wb;
    unsigned black[4];     /* 0x3d, in the file's bit depth */
    bool have_black;
    unsigned crop[6];      /* CropHiSpeed 0x1b: full w, h; crop w, h; left, top */
    bool have_crop;
} NikonMeta;

/* Nikon type-3 MakerNote: "Nikon\0" + 4 bytes, then a TIFF header at +10
 * whose offsets are relative to that header. */
static bool nikon_makernote(const Tiff *t, NikonMeta *m, unsigned bps)
{
    memset(m, 0, sizeof *m);
    const Bytes *b = &t->b;
    size_t off = t->makernote_off;
    if (!t->makernote_len || !in_range(b, off, 18)) return false;
    if (memcmp(b->data + off, "Nikon\0", 6)) return false;
    Bytes mb = *b;
    size_t base = off + 10;
    if (b->data[base] == 'M') mb.big_endian = true;
    else if (b->data[base] == 'I') mb.big_endian = false;
    else return false;
    size_t ifd = get32(&mb, base + 4);
    unsigned n = get16(&mb, base + ifd);
    if (n == 0 || n > 512) return false;
    Entry e;
    for (unsigned i = 0; i < n; ++i) {
        if (!read_entry(&mb, base, ifd, i, &e)) continue;
        if (e.tag == 0x0c && e.count == 4) {
            /* stored R, B, G, G */
            m->wb[0] = (float)entry_num(&mb, &e, 0);
            m->wb[2] = (float)entry_num(&mb, &e, 1);
            m->wb[1] = (float)entry_num(&mb, &e, 2);
            m->wb[3] = (float)entry_num(&mb, &e, 3);
            m->have_wb = m->wb[0] > 0.0f && m->wb[1] > 0.0f && m->wb[2] > 0.0f;
        } else if (e.tag == 0x3d && e.type == 3 && e.count == 4) {
            for (int c = 0; c < 4; ++c)
                m->black[c ^ c >> 1] = (unsigned)entry_num(&mb, &e, (uint32_t)c)
                                       >> (14 - (bps > 14 ? 14 : bps));
            m->have_black = true;
        } else if (e.tag == 0x96) {
            m->curve_off = e.value_off;
            m->curve_len = e.count;
        } else if (e.tag == 0x1b && e.type == 3 && e.count == 7) {
            /* the image area inside the sensor readout: Nikon leaves the
             * masked columns and rows in the file (6080x4028 for the
             * 6016x4016 D610) and records the crop here */
            for (int k = 0; k < 6; ++k) m->crop[k] = (unsigned)entry_num(&mb, &e, (uint32_t)(k + 1));
            m->have_crop = m->crop[2] > 0 && m->crop[3] > 0;
        }
    }
    if (m->curve_off && in_range(b, m->curve_off, 2)) {
        m->ver0 = b->data[m->curve_off];
        m->ver1 = b->data[m->curve_off + 1];
    }
    return true;
}

static bool nikon_decode(const Tiff *t, const NikonMeta *m, uint16_t *out,
                         unsigned *white_out, PjError *error)
{
    const Ifd *d = &t->raw;
    const Bytes *b = &t->b;
    if (!m->curve_off) { pj_set_error(error, "NEF: no linearization table in MakerNote"); return false; }
    unsigned bps = d->bps;
    if (bps != 12 && bps != 14) { pj_set_error(error, "NEF: %u-bit compressed data unsupported", bps); return false; }

    /* the metadata block is big-endian in every Nikon file seen */
    Bytes mb = *b; mb.big_endian = true;
    size_t at = m->curve_off + 2;
    if (m->ver0 == 0x49 || m->ver1 == 0x58) at += 2110;
    int tree = 0;
    if (m->ver0 == 0x46) tree = 2;
    if (bps == 14) tree += 3;
    int vpred[2][2];
    for (int i = 0; i < 4; ++i) { vpred[i >> 1][i & 1] = (int)get16(&mb, at); at += 2; }
    unsigned max = 1u << bps;
    uint16_t *curve = malloc(0x10000 * sizeof *curve);
    if (!curve) { pj_set_error(error, "out of memory"); return false; }
    for (unsigned i = 0; i < 0x10000; ++i) curve[i] = (uint16_t)(i < max ? i : max - 1);
    unsigned csize = get16(&mb, at); at += 2;
    unsigned step = 0;
    if (csize > 1) step = max / (csize - 1);
    unsigned split = 0;
    if (m->ver0 == 0x44 && m->ver1 == 0x20 && step > 0) {
        for (unsigned i = 0; i < csize; ++i) { curve[i * step] = (uint16_t)get16(&mb, at); at += 2; }
        for (unsigned i = 0; i < max; ++i)
            curve[i] = (uint16_t)((curve[i - i % step] * (step - i % step) +
                                   curve[i - i % step + step] * (i % step)) / step);
        split = get16(&mb, m->curve_off + 562);
    } else if (m->ver0 != 0x46 && csize <= 0x4001) {
        max = csize;
        for (unsigned i = 0; i < csize; ++i) { curve[i] = (uint16_t)get16(&mb, at); at += 2; }
    }
    while (max > 2 && curve[max - 2] == curve[max - 1]) max--;
    *white_out = curve[max - 1];   /* dcraw: maximum = curve[max-1] */

    Huff huff;
    huff_build(&huff, nikon_tree[tree], nikon_tree[tree] + 16);
    size_t data_off = (size_t)entry_num(b, &d->strip_offsets, 0);
    size_t data_len = d->strip_counts.count ? (size_t)entry_num(b, &d->strip_counts, 0)
                                            : b->size - data_off;
    if (!in_range(b, data_off, 1)) { pj_set_error(error, "NEF: bad strip offset"); free(curve); return false; }
    if (data_len > b->size - data_off) data_len = b->size - data_off;
    BitReader r = {b->data + data_off, b->data + data_off + data_len, 0, 0, false, 0, false};
    int hpred[2] = {0, 0};
    int min = 0;
    for (size_t row = 0; row < d->height; ++row) {
        if (split && row == split) {
            huff_build(&huff, nikon_tree[tree + 1], nikon_tree[tree + 1] + 16);
            min = 16;
            max += 32;
        }
        for (size_t col = 0; col < d->width; ++col) {
            int i = huff_decode(&r, &huff);
            int len = i & 15;
            int shl = i >> 4;
            int diff = (int)((((int)getbits(&r, len - shl) << 1) + 1) << shl >> 1);
            if (len > 0 && (diff & (1 << (len - 1))) == 0)
                diff -= (1 << len) - !shl;
            if (col < 2) hpred[col] = vpred[row & 1][col] += diff;
            else hpred[col & 1] += diff;
            int v = hpred[col & 1];
            if (v < 0) v = 0;
            if (v > 0x3fff) v = 0x3fff;
            if ((unsigned)(v + min) >= max) v = (int)max - 1 - min;
            out[row * d->width + col] = curve[v];
        }
        if (r.overrun) {
            pj_set_error(error, "NEF: compressed data ended early (row %zu)", row);
            free(curve);
            return false;
        }
    }
    free(curve);
    return true;
}

/* ---- uncompressed samples --------------------------------------------- */

static bool unpack_samples(const Bytes *b, size_t off, size_t len, unsigned bps,
                           size_t count, uint16_t *out)
{
    if (!in_range(b, off, len)) return false;
    const unsigned char *p = b->data + off;
    if (bps == 16) {
        if (len < count * 2) return false;
        for (size_t i = 0; i < count; ++i)
            out[i] = (uint16_t)(b->big_endian ? p[2 * i] << 8 | p[2 * i + 1]
                                              : p[2 * i + 1] << 8 | p[2 * i]);
        return true;
    }
    if (bps == 8) {
        if (len < count) return false;
        for (size_t i = 0; i < count; ++i) out[i] = p[i];
        return true;
    }
    /* 10/12/14-bit packed, MSB first within the byte stream (TIFF) */
    if (len * 8 < count * bps) return false;
    uint32_t acc = 0; int nbits = 0; size_t k = 0;
    for (size_t i = 0; i < count; ++i) {
        while (nbits < (int)bps) { acc = acc << 8 | p[k++]; nbits += 8; }
        out[i] = (uint16_t)((acc >> (nbits - bps)) & ((1u << bps) - 1));
        nbits -= bps;
    }
    return true;
}

/* ---- the decode: tiles/strips -> one uint16 plane set ---------------- */

typedef struct {
    const Tiff *t;
    uint16_t *pix;
    unsigned spp;
    size_t tw, th, across;
    bool tiled;
    PjError error;
    size_t ntiles;
    int failed;
} TileCtx;

static void decode_tiles_band(size_t t0, size_t t1, void *vctx)
{
    TileCtx *tc = vctx;
    const Tiff *t = tc->t;
    const Ifd *d = &t->raw;
    const Bytes *b = &t->b;
    size_t w = d->width, h = d->height, tw = tc->tw, th = tc->th;
    unsigned spp = tc->spp;
    const Entry *offs = tc->tiled ? &d->tile_offsets : &d->strip_offsets;
    const Entry *cnts = tc->tiled ? &d->tile_counts : &d->strip_counts;
    uint16_t *tile = malloc(tw * th * spp * sizeof *tile);
    if (!tile) { tc->failed = 1; pj_set_error(&tc->error, "out of memory"); return; }
    for (size_t ti = t0; ti < t1 && !tc->failed; ++ti) {
        size_t off = (size_t)entry_num(b, offs, (uint32_t)ti);
        size_t len = cnts->count > ti ? (size_t)entry_num(b, cnts, (uint32_t)ti) : b->size - off;
        if (!in_range(b, off, len)) { pj_set_error(&tc->error, "raw: tile %zu outside file", ti); tc->failed = 1; break; }
        size_t tx = tc->tiled ? (ti % tc->across) * tw : 0;
        size_t ty = tc->tiled ? (ti / tc->across) * th : ti * th;
        size_t rows = th, cols = tw;
        if (!tc->tiled && ty + rows > h) rows = h - ty;
        if (d->compression == 1) {
            if (!unpack_samples(b, off, len, d->bps, cols * rows * spp, tile)) {
                pj_set_error(&tc->error, "raw: tile %zu truncated", ti); tc->failed = 1; break;
            }
        } else {
            LJpeg j;
            if (!ljpeg_header(&j, b->data + off, len)) {
                pj_set_error(&tc->error, "raw: tile %zu is not lossless JPEG", ti); tc->failed = 1; break;
            }
            if ((size_t)j.wide * j.clrs != cols * spp || j.high != rows) {
                /* some encoders fold a Bayer tile as wide/2 x 2 components;
                 * any layout whose row carries cols*spp samples is fine */
                if ((size_t)j.wide * j.clrs * j.high != cols * rows * spp) {
                    pj_set_error(&tc->error, "raw: tile %zu geometry %ux%ux%u does not match %zux%zux%u",
                                 ti, j.wide, j.high, j.clrs, cols, rows, spp);
                    tc->failed = 1; break;
                }
            }
            if (!ljpeg_decode(&j, tile)) {
                pj_set_error(&tc->error, "raw: lossless JPEG data in tile %zu ended early", ti);
                tc->failed = 1; break;
            }
            if (j.point)
                for (size_t i = 0; i < cols * rows * spp; ++i) tile[i] = (uint16_t)(tile[i] << j.point);
        }
        for (size_t y = 0; y < rows && ty + y < h; ++y) {
            size_t n = cols;
            if (tx + n > w) n = w - tx;
            memcpy(tc->pix + ((ty + y) * w + tx) * spp, tile + y * cols * spp, n * spp * sizeof *tc->pix);
        }
    }
    free(tile);
}

static bool decode_pixels(const Tiff *t, uint16_t *pix, unsigned spp,
                          unsigned *nikon_white, PjError *error)
{
    const Ifd *d = &t->raw;
    const Bytes *b = &t->b;
    size_t w = d->width, h = d->height;
    if (d->compression == 34713) {
        NikonMeta m;
        if (!nikon_makernote(t, &m, d->bps)) {
            pj_set_error(error, "NEF: Nikon MakerNote not found");
            return false;
        }
        return nikon_decode(t, &m, pix, nikon_white, error);
    }
    bool tiled = d->tile_w && d->tile_h && d->tile_offsets.count;
    size_t tw = tiled ? d->tile_w : w;
    size_t th = tiled ? d->tile_h : (d->rows_per_strip.count ? (size_t)entry_num(b, &d->rows_per_strip, 0) : h);
    if (th == 0 || th > h) th = h;
    const Entry *offs = tiled ? &d->tile_offsets : &d->strip_offsets;
    size_t across = (w + tw - 1) / tw, down = (h + th - 1) / th;
    size_t ntiles = tiled ? across * down : down;
    if (offs->count < ntiles) { pj_set_error(error, "raw: too few strip/tile offsets"); return false; }
    if (d->compression != 1 && d->compression != 7) {
        pj_set_error(error, "raw: compression %u unsupported (uncompressed, lossless JPEG, Nikon)", d->compression);
        return false;
    }
    /* tiles are independent streams: decode them in parallel, each
     * band of tiles with its own scratch buffer */
    TileCtx tc = {t, pix, spp, tw, th, across, tiled, {{0}}, ntiles, 0};
    pj_parallel_rows(ntiles, decode_tiles_band, &tc);
    if (tc.failed) {
        pj_set_error(error, "%s", tc.error.message[0] ? tc.error.message : "raw: tile decode failed");
        return false;
    }
    return true;
}

/* ---- color ------------------------------------------------------------ */

/* Camera->XYZ matrices for NEF, which carries none. Adobe-style
 * "ColorMatrix" (XYZ->camera, D65), x1e4, as dcraw's table lists them;
 * provenance in docs/PRESET_SOURCES.md. */
typedef struct { const char *model; short m[9]; } CamMatrix;
static const CamMatrix cam_matrices[] = {
    { "D610",  { 8178,-2577,-1137,-6219,13925,2511,-1527,2951,6721 } },
    { "D600",  { 8178,-2577,-1137,-6219,13925,2511,-1527,2951,6721 } },
    { "D3200", { 7013,-1408,-635,-5268,12902,2640,-1470,2801,7379 } },
    { "D3300", { 6988,-1384,-714,-5631,13410,2447,-1485,2204,7318 } },
    { "D750",  { 9020,-2890,-715,-4535,12436,2348,-934,1919,7086 } },
    { "D7000", { 8198,-2239,-724,-4871,12389,2798,-1043,2050,7181 } },
    { "D800",  { 7866,-2108,-555,-4869,12483,2681,-1176,2069,7501 } },
    { "D90",   { 7309,-1403,-519,-8474,16008,2622,-2434,2826,8064 } },
};

static bool invert3(const double m[9], double out[9])
{
    double det = m[0] * (m[4] * m[8] - m[5] * m[7])
               - m[1] * (m[3] * m[8] - m[5] * m[6])
               + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (fabs(det) < 1e-12) return false;
    double inv[9] = {
        (m[4] * m[8] - m[5] * m[7]), -(m[1] * m[8] - m[2] * m[7]),  (m[1] * m[5] - m[2] * m[4]),
       -(m[3] * m[8] - m[5] * m[6]),  (m[0] * m[8] - m[2] * m[6]), -(m[0] * m[5] - m[2] * m[3]),
        (m[3] * m[7] - m[4] * m[6]), -(m[0] * m[7] - m[1] * m[6]),  (m[0] * m[4] - m[1] * m[3]) };
    for (int i = 0; i < 9; ++i) out[i] = inv[i] / det;
    return true;
}

/* dcraw's recipe: cam_rgb = ColorMatrix(XYZ->cam) * (sRGB->XYZ), rows
 * normalized to sum 1 so that camera white maps to RGB white after the
 * white balance, then inverted to give rgb_cam (camera->sRGB linear). */
static bool build_rgb_cam(const double xyz_cam[9], double rgb_cam[9])
{
    static const double srgb_xyz[9] = {
        0.412453, 0.357580, 0.180423,
        0.212671, 0.715160, 0.072169,
        0.019334, 0.119193, 0.950227 };
    double cam_rgb[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            cam_rgb[i * 3 + j] = 0.0;
            for (int k = 0; k < 3; ++k)
                cam_rgb[i * 3 + j] += xyz_cam[i * 3 + k] * srgb_xyz[k * 3 + j];
        }
    for (int i = 0; i < 3; ++i) {
        double s = cam_rgb[i * 3] + cam_rgb[i * 3 + 1] + cam_rgb[i * 3 + 2];
        if (fabs(s) < 1e-9) return false;
        for (int j = 0; j < 3; ++j) cam_rgb[i * 3 + j] /= s;
    }
    return invert3(cam_rgb, rgb_cam);
}

/* ---- raw samples -> scene-linear RGB, in row bands -------------------- */

typedef struct { uint16_t *pix; const uint16_t *table; size_t n, stride; } LinCtx;

static void linearize_band(size_t y0, size_t y1, void *vctx)
{
    LinCtx *l = vctx;
    for (size_t i = y0 * l->stride; i < y1 * l->stride; ++i) {
        uint32_t v = l->pix[i];
        if (v >= l->n) v = (uint32_t)l->n - 1;
        l->pix[i] = l->table[v];
    }
}

typedef struct {
    const uint16_t *pix;
    float *plane;          /* normalized Bayer plane (CFA path) */
    float *rgb;            /* output */
    size_t w, h;
    bool linear_raw;
    double black[4], white;
    double wb[3], rgb_cam[9];
    unsigned cfa[4];
} ConvertCtx;

static void normalize_band(size_t y0, size_t y1, void *vctx)
{
    ConvertCtx *c = vctx;
    size_t w = c->w;
    if (c->linear_raw) {
        for (size_t i = y0 * w; i < y1 * w; ++i)
            for (int ch = 0; ch < 3; ++ch) {
                double scale = c->white - c->black[ch];
                double v = scale > 0 ? (c->pix[i * 3 + ch] - c->black[ch]) / scale : 0.0;
                c->rgb[i * 3 + ch] = (float)(v < 0 ? 0 : v);
            }
        return;
    }
    for (size_t y = y0; y < y1; ++y)
        for (size_t x = 0; x < w; ++x) {
            unsigned cell = (unsigned)((y & 1) * 2 + (x & 1));
            double scale = c->white - c->black[cell];
            double v = scale > 0 ? (c->pix[y * w + x] - c->black[cell]) / scale : 0.0;
            c->plane[y * w + x] = (float)(v < 0 ? 0 : v);
        }
}

static void demosaic_band(size_t y0, size_t y1, void *vctx)
{
    ConvertCtx *c = vctx;
    size_t w = c->w, h = c->h;
    const float *plane = c->plane;
    for (size_t y = y0; y < y1; ++y)
        for (size_t x = 0; x < w; ++x) {
            float sum[3] = {0, 0, 0}; int cnt[3] = {0, 0, 0};
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    long yy = (long)y + dy, xx = (long)x + dx;
                    if (yy < 0) yy = 1;
                    if (xx < 0) xx = 1;
                    if (yy >= (long)h) yy = (long)h - 2;
                    if (xx >= (long)w) xx = (long)w - 2;
                    unsigned ch = c->cfa[((unsigned)yy & 1) * 2 + ((unsigned)xx & 1)];
                    /* a 3x3 window mixes the same-color diagonal
                     * neighbors with the center for R/B at a G site;
                     * weight the center fully, neighbors by 1 */
                    sum[ch] += plane[(size_t)yy * w + (size_t)xx];
                    cnt[ch]++;
                }
            unsigned here = c->cfa[(y & 1) * 2 + (x & 1)];
            for (int ch = 0; ch < 3; ++ch)
                c->rgb[(y * w + x) * 3 + ch] = (ch == (int)here) ? plane[y * w + x]
                                             : (cnt[ch] ? sum[ch] / (float)cnt[ch] : 0.0f);
        }
}

/* white balance, clip to neutral white, camera->sRGB; in place on rgb */
static void color_band(size_t y0, size_t y1, void *vctx)
{
    ConvertCtx *c = vctx;
    size_t w = c->w;
    for (size_t i = y0 * w; i < y1 * w; ++i) {
        float *px = &c->rgb[i * 3];
        double r = px[0] * c->wb[0], g = px[1] * c->wb[1], bb = px[2] * c->wb[2];
        /* A saturated sensor channel knows nothing above white, and the
         * white-balance gains would otherwise paint every clipped
         * highlight magenta: clip to neutral white here (dcraw's default
         * highlight handling). The headroom polajuice works with is the
         * stop the pipeline opens the image up by, not light the sensor
         * never saw. */
        if (r > 1.0) r = 1.0;
        if (g > 1.0) g = 1.0;
        if (bb > 1.0) bb = 1.0;
        for (int ch = 0; ch < 3; ++ch) {
            double v = c->rgb_cam[ch * 3] * r + c->rgb_cam[ch * 3 + 1] * g + c->rgb_cam[ch * 3 + 2] * bb;
            px[ch] = (float)(v < 0 ? 0 : v);
        }
    }
}

/* ---- the door --------------------------------------------------------- */

static unsigned char *slurp(const char *path, size_t *size, PjError *error)
{
    FILE *f = fopen(path, "rb");
    if (!f) { pj_set_error(error, "cannot open '%s'", path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); pj_set_error(error, "cannot read '%s'", path); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); pj_set_error(error, "'%s' is empty", path); return NULL; }
    rewind(f);
    unsigned char *buf = malloc((size_t)n);
    if (!buf) { fclose(f); pj_set_error(error, "out of memory"); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); pj_set_error(error, "cannot read '%s'", path); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

/* Last raw file's structure, for 'inspect': container, layout, tiling,
 * compression, bit depth, camera. Static text, overwritten per load. */
static char raw_description[256];
const char *pj_raw_last_description(void) { return raw_description; }
const char *pj_raw_description(void) { return raw_description; }

PjImage *pj_image_load_raw(const char *path, PjError *error)
{
    size_t size = 0;
    unsigned char *data = slurp(path, &size, error);
    if (!data) return NULL;
    Tiff t;
    memset(&t, 0, sizeof t);
    t.b.data = data; t.b.size = size;
    if (!parse_tiff(&t, error)) { free(data); return NULL; }
    const Ifd *d = &t.raw;
    bool linear_raw = d->photometric == 34892;
    unsigned spp = linear_raw ? d->spp : 1;
    if (linear_raw && spp != 3) {
        pj_set_error(error, "raw: LinearRaw with %u samples per pixel unsupported", spp);
        free(data); return NULL;
    }
    if (!linear_raw && d->photometric != 32803 && d->compression != 34713) {
        pj_set_error(error, "raw: photometric %u is not CFA or LinearRaw", d->photometric);
        free(data); return NULL;
    }
    size_t w = d->width, h = d->height;
    if (w < 4 || h < 4 || w > 20000 || h > 20000) {
        pj_set_error(error, "raw: implausible dimensions %zux%zu", w, h);
        free(data); return NULL;
    }
    uint16_t *pix = malloc(w * h * spp * sizeof *pix);
    if (!pix) { pj_set_error(error, "out of memory"); free(data); return NULL; }
    unsigned nikon_white = 0;
    if (!decode_pixels(&t, pix, spp, &nikon_white, error)) { free(pix); free(data); return NULL; }

    /* The image area. Sensors read out masked (optically black) columns
     * and rows around the picture; DNG says where the picture is with
     * ActiveArea and DefaultCropOrigin/Size, Nikon with CropHiSpeed in
     * the MakerNote. Cut the samples down to it before anything else, so
     * the demosaic, the statistics and the picture never see the black
     * border; a crop at an odd offset shifts the CFA phase. */
    size_t crop_x = 0, crop_y = 0, crop_w = w, crop_h = h;
    {
        const Bytes *cb = &t.b;
        if (t.is_dng) {
            if (d->have_active && d->active_area.count >= 4) {
                size_t top = (size_t)entry_num(cb, &d->active_area, 0), left = (size_t)entry_num(cb, &d->active_area, 1);
                size_t bottom = (size_t)entry_num(cb, &d->active_area, 2), right = (size_t)entry_num(cb, &d->active_area, 3);
                if (left < right && top < bottom && right <= w && bottom <= h) {
                    crop_x = left; crop_y = top; crop_w = right - left; crop_h = bottom - top;
                }
            }
            if (d->have_crop_origin && d->have_crop_size &&
                d->crop_origin.count >= 2 && d->crop_size.count >= 2) {
                size_t ox = (size_t)entry_num(cb, &d->crop_origin, 0), oy = (size_t)entry_num(cb, &d->crop_origin, 1);
                size_t sw = (size_t)entry_num(cb, &d->crop_size, 0), sh = (size_t)entry_num(cb, &d->crop_size, 1);
                if (sw > 0 && sh > 0 && ox + sw <= crop_w && oy + sh <= crop_h) {
                    crop_x += ox; crop_y += oy; crop_w = sw; crop_h = sh;
                }
            }
        } else {
            NikonMeta nm;
            if (nikon_makernote(&t, &nm, d->bps) && nm.have_crop &&
                nm.crop[0] == w && nm.crop[1] == h &&
                nm.crop[4] + nm.crop[2] <= w && nm.crop[5] + nm.crop[3] <= h) {
                crop_x = nm.crop[4]; crop_y = nm.crop[5]; crop_w = nm.crop[2]; crop_h = nm.crop[3];
            }
        }
        if (crop_w < 4 || crop_h < 4) { crop_x = crop_y = 0; crop_w = w; crop_h = h; }
    }
    unsigned cfa_shift_x = (unsigned)(crop_x & 1), cfa_shift_y = (unsigned)(crop_y & 1);
    if (crop_w != w || crop_h != h) {
        /* compact in place: every destination lies at or before its source */
        for (size_t y = 0; y < crop_h; ++y)
            memmove(pix + y * crop_w * spp, pix + ((y + crop_y) * w + crop_x) * spp,
                    crop_w * spp * sizeof *pix);
        w = crop_w; h = crop_h;
    }

    /* linearization, black and white levels */
    const Bytes *b = &t.b;
    double black[4] = {0, 0, 0, 0}, white = (double)((1u << (d->bps > 16 ? 16 : d->bps)) - 1);
    if (nikon_white) white = nikon_white;
    if (d->have_lin && d->lin_table.count > 1) {
        size_t n = d->lin_table.count;
        if (n > 65536) n = 65536;
        uint16_t *table = malloc(n * sizeof *table);
        if (!table) { pj_set_error(error, "out of memory"); free(pix); free(data); return NULL; }
        for (size_t i = 0; i < n; ++i) table[i] = (uint16_t)entry_num(b, &d->lin_table, (uint32_t)i);
        LinCtx lc = {pix, table, n, w * spp};
        pj_parallel_rows(h, linearize_band, &lc);
        white = table[n - 1];
        free(table);
    }
    if (d->have_white) white = entry_num(b, &d->white, 0);
    if (d->have_black) {
        uint32_t n = d->black.count;
        if (n == 1) black[0] = black[1] = black[2] = black[3] = entry_num(b, &d->black, 0);
        else if (n == 4 && !linear_raw)
            for (int c = 0; c < 4; ++c) black[c] = entry_num(b, &d->black, (uint32_t)c);
        else if (n == 3 && linear_raw)
            for (int c = 0; c < 3; ++c) black[c] = entry_num(b, &d->black, (uint32_t)c);
        else { double s = 0; for (uint32_t i = 0; i < n; ++i) s += entry_num(b, &d->black, i);
               black[0] = black[1] = black[2] = black[3] = s / n; }
    }
    /* CFA pattern: 2x2 of 0=R 1=G 2=B */
    unsigned cfa[4] = {0, 1, 1, 2};
    if (!linear_raw && d->have_cfa_pattern && d->cfa_pattern.count >= 4)
        for (int c = 0; c < 4; ++c) cfa[c] = (unsigned)entry_num(b, &d->cfa_pattern, (uint32_t)c) & 3;
    if (cfa_shift_x || cfa_shift_y) {
        /* the crop moved the origin: cell (y,x) of the picture is cell
         * (y+sy, x+sx) of the sensor, for the pattern and the per-cell black */
        unsigned cfa2[4]; double black2[4];
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 2; ++x) {
                unsigned from = ((y + cfa_shift_y) & 1) * 2 + ((x + cfa_shift_x) & 1);
                cfa2[y * 2 + x] = cfa[from];
                black2[y * 2 + x] = black[from];
            }
        memcpy(cfa, cfa2, sizeof cfa);
        memcpy(black, black2, sizeof black);
    }

    /* white balance multipliers and camera->sRGB matrix */
    double wb[3] = {1, 1, 1};
    double xyz_cam[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool have_matrix = false;
    if (t.is_dng) {
        const Entry *as = d->have_as_shot ? &d->as_shot : (t.have_as_shot_0 ? &t.as_shot_0 : NULL);
        if (as && as->count >= 3)
            for (int c = 0; c < 3; ++c) { double v = entry_num(b, as, (uint32_t)c); wb[c] = v > 1e-6 ? 1.0 / v : 1.0; }
        /* prefer the D65-ish matrix: illuminant 21 = D65; else the second
         * when present, else the first */
        const Entry *cm = NULL;
        const Entry *c1 = d->have_cm1 ? &d->cm1 : (t.have_cm1_0 ? &t.cm1_0 : NULL);
        const Entry *c2 = d->have_cm2 ? &d->cm2 : (t.have_cm2_0 ? &t.cm2_0 : NULL);
        const Entry *i1 = d->have_cm1 ? &d->illum1 : &t.illum1_0;
        if (c1 && c1->count >= 9 && i1->count && (unsigned)entry_num(b, i1, 0) == 21) cm = c1;
        else if (c2 && c2->count >= 9) cm = c2;
        else if (c1 && c1->count >= 9) cm = c1;
        if (cm) { for (int i = 0; i < 9; ++i) xyz_cam[i] = entry_num(b, cm, (uint32_t)i); have_matrix = true; }
    } else {
        NikonMeta m;
        if (nikon_makernote(&t, &m, d->bps)) {
            if (m.have_wb) { wb[0] = m.wb[0]; wb[1] = m.wb[1]; wb[2] = m.wb[2]; }
            if (m.have_black && !d->have_black)
                for (int c = 0; c < 4; ++c) black[c] = m.black[c];
        }
        for (size_t i = 0; i < sizeof cam_matrices / sizeof cam_matrices[0]; ++i)
            if (strstr(t.model, cam_matrices[i].model)) {
                for (int k = 0; k < 9; ++k) xyz_cam[k] = cam_matrices[i].m[k] / 10000.0;
                have_matrix = true;
                break;
            }
    }
    /* normalize so green stays at 1 */
    if (wb[1] > 1e-9) { wb[0] /= wb[1]; wb[2] /= wb[1]; wb[1] = 1.0; }
    double rgb_cam[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (have_matrix && !build_rgb_cam(xyz_cam, rgb_cam)) {
        for (int i = 0; i < 9; ++i) rgb_cam[i] = (i % 4 == 0) ? 1.0 : 0.0;
    }

    PjImage *image = pj_image_new(w, h, error);
    if (!image) { free(pix); free(data); return NULL; }

    /* scene-linear camera RGB, black removed, white at 1.0; the image
     * buffer itself is the work buffer, so no 144 MB intermediate */
    ConvertCtx cc = {.pix = pix, .plane = NULL, .rgb = image->rgb, .w = w, .h = h,
                     .linear_raw = linear_raw, .white = white};
    memcpy(cc.black, black, sizeof black);
    memcpy(cc.wb, wb, sizeof wb);
    memcpy(cc.rgb_cam, rgb_cam, sizeof rgb_cam);
    memcpy(cc.cfa, cfa, sizeof cfa);
    if (!linear_raw) {
        cc.plane = malloc(w * h * sizeof *cc.plane);
        if (!cc.plane) { pj_set_error(error, "out of memory"); pj_image_free(image); free(pix); free(data); return NULL; }
    }
    pj_parallel_rows(h, normalize_band, &cc);
    if (!linear_raw) {
        pj_parallel_rows(h, demosaic_band, &cc);
        free(cc.plane);
    }
    free(pix);
    pj_parallel_rows(h, color_band, &cc);
    image->scene_linear = true;
    {
        bool tiled = d->tile_w && d->tile_h && d->tile_offsets.count;
        size_t ntiles = tiled ? ((w + d->tile_w - 1) / d->tile_w) * ((h + d->tile_h - 1) / d->tile_h)
                              : d->strip_offsets.count;
        const char *comp = d->compression == 1 ? "uncompressed"
                         : d->compression == 7 ? "lossless JPEG"
                         : d->compression == 34713 ? "Nikon compressed" : "other";
        snprintf(raw_description, sizeof raw_description,
                 "%s, %s, %u-bit, %s, %zu %s%s%s%s%s",
                 t.is_dng ? "DNG" : "NEF",
                 linear_raw ? "LinearRaw (demosaiced in camera)" : "CFA (Bayer)",
                 d->bps, comp, ntiles, tiled ? "tile" : "strip",
                 ntiles == 1 ? "" : "s",
                 t.model[0] ? ", " : "", t.make[0] ? t.make : "", t.model[0] ? t.model : "");
        if (t.make[0] && t.model[0] && !strstr(t.model, t.make)) {
            /* "NIKON CORPORATION" + "NIKON D610": the model already names
             * the maker in most files; otherwise separate the two */
            char tmp[256];
            snprintf(tmp, sizeof tmp, "%s", raw_description);
            char *at = strstr(tmp, t.make);
            if (at) {
                size_t pre = (size_t)(at - tmp);
                snprintf(raw_description, sizeof raw_description, "%.*s%s %s",
                         (int)pre, tmp, t.make, t.model);
            }
        } else if (t.model[0]) {
            char tmp[256];
            snprintf(tmp, sizeof tmp, "%s", raw_description);
            char *at = t.make[0] ? strstr(tmp, t.make) : NULL;
            if (at) {
                size_t pre = (size_t)(at - tmp);
                snprintf(raw_description, sizeof raw_description, "%.*s%s", (int)pre, tmp, t.model);
            }
        }
    }
    unsigned orientation = d->orientation > 1 ? d->orientation : t.orientation0;
    free(data);
    return pj_image_apply_orientation(image, (int)orientation, error);
}

/* 16-bit PNG (or 8-bit, scaled) taken as scene-linear without a gamma
 * decode; 16-bit uncompressed TIFF goes through the walker above. */
PjImage *pj_image_load_linear(const char *path, PjError *error)
{
    const char *dot = strrchr(path, '.');
    bool is_tiff = dot && (!strcmp(dot, ".tif") || !strcmp(dot, ".tiff") ||
                           !strcmp(dot, ".TIF") || !strcmp(dot, ".TIFF"));
    if (is_tiff) {
        size_t size = 0;
        unsigned char *data = slurp(path, &size, error);
        if (!data) return NULL;
        Tiff t; memset(&t, 0, sizeof t);
        t.b.data = data; t.b.size = size;
        /* reuse the walker, but accept RGB directories */
        if (size < 8 || get16(&(Bytes){data, size, data[0] == 'M'}, 2) != 42) {
            pj_set_error(error, "'%s' is not a TIFF", path); free(data); return NULL;
        }
        t.b.big_endian = data[0] == 'M';
        parse_ifd(&t, get32(&t.b, 4), 0);
        /* the walker only keeps CFA/LinearRaw; walk IFD0 again for RGB */
        Ifd d = {0}; d.spp = 1; d.bps = 8; d.compression = 1;
        size_t ifd = get32(&t.b, 4);
        unsigned n = get16(&t.b, ifd);
        Entry e;
        for (unsigned i = 0; i < n && n < 512; ++i) {
            if (!read_entry(&t.b, 0, ifd, i, &e)) continue;
            switch (e.tag) {
            case 256: d.width = (size_t)entry_num(&t.b, &e, 0); break;
            case 257: d.height = (size_t)entry_num(&t.b, &e, 0); break;
            case 258: d.bps = (unsigned)entry_num(&t.b, &e, 0); break;
            case 259: d.compression = (unsigned)entry_num(&t.b, &e, 0); break;
            case 262: d.photometric = (unsigned)entry_num(&t.b, &e, 0); break;
            case 273: d.strip_offsets = e; break;
            case 274: d.orientation = (unsigned)entry_num(&t.b, &e, 0); break;
            case 277: d.spp = (unsigned)entry_num(&t.b, &e, 0); break;
            case 278: d.rows_per_strip = e; break;
            case 279: d.strip_counts = e; break;
            default: break;
            }
        }
        if (d.compression != 1 || d.spp < 3 || (d.bps != 8 && d.bps != 16) || !d.width || !d.height) {
            pj_set_error(error, "'%s': --linear takes uncompressed 8/16-bit RGB TIFF", path);
            free(data); return NULL;
        }
        t.raw = d;
        uint16_t *pix = malloc(d.width * d.height * d.spp * sizeof *pix);
        if (!pix) { pj_set_error(error, "out of memory"); free(data); return NULL; }
        unsigned unused = 0;
        if (!decode_pixels(&t, pix, d.spp, &unused, error)) { free(pix); free(data); return NULL; }
        PjImage *image = pj_image_new(d.width, d.height, error);
        if (!image) { free(pix); free(data); return NULL; }
        float scale = d.bps == 16 ? 1.0f / 65535.0f : 1.0f / 255.0f;
        for (size_t i = 0; i < d.width * d.height; ++i)
            for (int c = 0; c < 3; ++c)
                image->rgb[i * 3 + c] = pix[i * d.spp + c] * scale;
        free(pix); free(data);
        image->scene_linear = true;
        return pj_image_apply_orientation(image, (int)d.orientation, error);
    }
    int width = 0, height = 0, channels = 0;
    unsigned short *p16 = stbi_load_16(path, &width, &height, &channels, 3);
    if (!p16) {
        pj_set_error(error, "cannot decode '%s': %s", path, stbi_failure_reason());
        return NULL;
    }
    PjImage *image = pj_image_new((size_t)width, (size_t)height, error);
    if (!image) { stbi_image_free(p16); return NULL; }
    size_t samples = (size_t)width * (size_t)height * 3;
    for (size_t i = 0; i < samples; ++i) image->rgb[i] = p16[i] / 65535.0f;
    stbi_image_free(p16);
    image->scene_linear = true;
    return image;
}
