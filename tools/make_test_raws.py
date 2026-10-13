#!/usr/bin/env python3
"""
Generate the tiny synthetic raw files under tests/raw/ that exercise the
scene-linear door without shipping anyone's photograph:

  bayer_u16.dng      uncompressed 16-bit RGGB Bayer, stripped
  bayer_lj92.dng     the same scene, lossless-JPEG tiles (two components
                     per sample pair, as Adobe writes it), predictor 1
  linear_lj92.dng    LinearRaw (3 samples/pixel, the ProRAW layout),
                     lossless-JPEG tile, predictor 6, BlackLevel per sample
  lossless14.nef     Nikon-compressed 14-bit lossless (tree 3), type-3
                     MakerNote with 0x96 curve, 0x0c WB, 0x3d black
  linear16.png       16-bit PNG of the same scene with no gamma, for --linear

All encode one scene: a horizontal exposure ramp from 0 to 3.0x sensor
white (so the top stop and a half clip in the raw but a specular patch
sits at exactly white), a gray card at 0.18 in the middle, and a saturated
red and blue patch, so the tests can check black/white scaling, white
balance, the color matrix path and the above-white headroom carried out.

Scene values are camera-RGB before white balance; the files declare
AsShotNeutral / WB so that the decoder has real work to do.
"""
import struct, sys, zlib, os
from collections import Counter

W, H = 64, 48
WHITE = 4000          # sensor white level (of 4095 for 12-bit, scaled for 14)
BLACK = 200

def scene(x, y):
    """linear camera-RGB *after* white balance, white = 1.0"""
    if 20 <= y < 28 and 24 <= x < 40:
        return (0.18, 0.18, 0.18)           # gray card
    if 32 <= y < 44 and 8 <= x < 20:
        return (0.6, 0.05, 0.05)            # red patch
    if 32 <= y < 44 and 44 <= x < 56:
        return (0.05, 0.05, 0.6)            # blue patch
    if 4 <= y < 12 and 54 <= x < 60:
        return (1.0, 1.0, 1.0)              # exactly white patch
    t = x / (W - 1)
    v = 3.0 * t                             # ramp to 3x white: clips at 1.0
    return (v, v, v)

WB = (2.0, 1.0, 1.5)     # as-shot R,G,B multipliers (camera needs these)
CFA = [0, 1, 1, 2]       # RGGB

def cam_sample(x, y, ch):
    r, g, b = scene(x, y)
    v = (r, g, b)[ch] / WB[ch]            # un-white-balance
    return v

def bayer_plane(bits, white=None):
    blk = BLACK * (1 << (bits - 12))
    scale = (white if white else WHITE * (1 << (bits - 12))) - blk
    out = []
    for y in range(H):
        for x in range(W):
            ch = CFA[(y & 1) * 2 + (x & 1)]
            v = cam_sample(x, y, ch) * scale + blk
            v = int(round(min(v, (1 << bits) - 1)))
            out.append(v)
    return out

def linear_pixels(bits):
    scale = WHITE * (1 << (bits - 12)) - BLACK * (1 << (bits - 12))
    blk = BLACK * (1 << (bits - 12))
    out = []
    for y in range(H):
        for x in range(W):
            for ch in range(3):
                v = cam_sample(x, y, ch) * scale + blk
                out.append(int(round(min(v, (1 << bits) - 1))))
    return out

# ---- TIFF writer ---------------------------------------------------------

def tiff(entries, blobs, big_endian=False, extra_ifds=()):
    """entries: list of (tag, type, values) sorted by tag; values may be a
    bytes blob (type 1/7/2) or a list of numbers. blobs: dict name->bytes
    placed after the IFD, referenced by ('blob', name) values which become
    offsets. Returns bytes."""
    E = '>' if big_endian else '<'
    tsize = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 7: 1, 9: 4, 10: 8, 11: 4, 12: 8}
    header = (b'MM' if big_endian else b'II') + struct.pack(E + 'HI', 42, 8)
    # layout: header(8) | IFD | values area | blobs
    ifd_len = 2 + 12 * len(entries) + 4
    values_off = 8 + ifd_len
    values = b''
    blob_offsets = {}
    # first pass: blobs come after values; compute values size needs blob offsets
    # for entries that reference blobs -> two-pass
    def pack_values(ent, blob_offsets):
        tag, typ, vals = ent
        if isinstance(vals, bytes):
            return vals
        out = b''
        for v in vals:
            if isinstance(v, tuple) and v[0] == 'blob':
                v = blob_offsets.get(v[1], 0)
            if typ == 3: out += struct.pack(E + 'H', v)
            elif typ == 4: out += struct.pack(E + 'I', v)
            elif typ == 5: out += struct.pack(E + 'II', *v)
            elif typ == 10: out += struct.pack(E + 'ii', *v)
            elif typ == 9: out += struct.pack(E + 'i', v)
            elif typ == 1 or typ == 7: out += struct.pack('B', v)
            elif typ == 8: out += struct.pack(E + 'h', v)
            else: raise ValueError(typ)
        return out
    # sizes
    sizes = []
    for ent in entries:
        tag, typ, vals = ent
        if isinstance(vals, bytes): n = len(vals)
        else: n = len(vals) * tsize[typ]
        sizes.append(n)
    total_values = sum(n for n in sizes if n > 4) + sum(n % 2 for n in sizes if n > 4)
    blobs_off = values_off + total_values
    off = blobs_off
    for name, data in blobs.items():
        blob_offsets[name] = off
        off += len(data) + (len(data) & 1)
    ifd = struct.pack(E + 'H', len(entries))
    vpos = values_off
    for ent, n in zip(entries, sizes):
        tag, typ, vals = ent
        data = pack_values(ent, blob_offsets)
        count = len(vals) if not isinstance(vals, bytes) else len(vals)
        if typ in (5, 10): count = len(vals)
        if n <= 4:
            ifd += struct.pack(E + 'HHI', tag, typ, count) + data.ljust(4, b'\0')
        else:
            ifd += struct.pack(E + 'HHII', tag, typ, count, vpos)
            values += data + (b'\0' if len(data) & 1 else b'')
            vpos += len(data) + (len(data) & 1)
    ifd += struct.pack(E + 'I', 0)
    out = header + ifd + values
    assert len(out) == blobs_off, (len(out), blobs_off)
    for name, data in blobs.items():
        out += data + (b'\0' if len(data) & 1 else b'')
    return out

# ---- lossless JPEG encoder ------------------------------------------------

def huff_tables_for(symbols):
    """Build a JPEG DHT (counts, symbols) from symbol frequencies: lengths
    assigned by a simple length-limited scheme (all codes <= 16)."""
    freq = Counter(symbols)
    syms = sorted(freq, key=lambda s: (-freq[s], s))
    # assign lengths 1.. based on rank, keeping Kraft sum <= 1
    n = len(syms)
    lengths = {}
    # simple: give the most frequent short codes, pad others
    k = 0
    budget = 1.0
    for i, s in enumerate(syms):
        remaining = n - i
        # smallest length l such that 2^-l * remaining <= budget
        l = 1
        while (2.0 ** -l) * remaining > budget + 1e-12:
            l += 1
        l = min(max(l, 1), 16)
        lengths[s] = l
        budget -= 2.0 ** -l
    counts = [0] * 16
    for s in syms:
        counts[lengths[s] - 1] += 1
    ordered = sorted(syms, key=lambda s: (lengths[s], s))
    return counts, ordered

def huff_codes(counts, ordered):
    codes = {}
    code = 0
    k = 0
    for l in range(1, 17):
        for _ in range(counts[l - 1]):
            codes[ordered[k]] = (code, l)
            code += 1
            k += 1
        code <<= 1
    return codes

class BitWriter:
    def __init__(self):
        self.out = bytearray(); self.acc = 0; self.n = 0
    def put(self, value, nbits):
        for i in range(nbits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                if self.acc == 0xFF: self.out.append(0)
                self.acc = 0; self.n = 0
    def flush(self):
        while self.n: self.put(1, 1)
        return bytes(self.out)

def ljpeg_encode(samples, wide, high, clrs, bits, pred):
    """samples: high rows of wide*clrs values. Returns the JPEG byte string."""
    diffs = []
    rowlen = wide * clrs
    for row in range(high):
        for col in range(wide):
            for c in range(clrs):
                i = row * rowlen + col * clrs + c
                if row == 0:
                    p = (1 << (bits - 1)) if col == 0 else samples[i - clrs]
                elif col == 0:
                    p = samples[i - rowlen]
                else:
                    ra, rb, rc = samples[i - clrs], samples[i - rowlen], samples[i - rowlen - clrs]
                    p = {1: ra, 2: rb, 3: rc, 4: ra + rb - rc, 5: ra + ((rb - rc) >> 1),
                         6: rb + ((ra - rc) >> 1), 7: (ra + rb) >> 1}[pred]
                d = samples[i] - p
                # wrap to 16 bits signed like the decoder's modular arithmetic
                d = ((d + 32768) & 0xFFFF) - 32768
                diffs.append(d)
    def cat(d):
        d = abs(d); n = 0
        while d: n += 1; d >>= 1
        return n
    cats = [cat(d) for d in diffs]
    counts, ordered = huff_tables_for(cats)
    codes = huff_codes(counts, ordered)
    bw = BitWriter()
    for d, s in zip(diffs, cats):
        code, l = codes[s]
        bw.put(code, l)
        if s and s < 16:
            v = d if d > 0 else d + (1 << s) - 1
            bw.put(v, s)
    scan = bw.flush()
    sof = struct.pack('>BHHB', bits, high, wide, clrs) + b''.join(struct.pack('BBB', c + 1, 0x11, 0) for c in range(clrs))
    dht = bytes([0x00]) + bytes(counts) + bytes(ordered)
    sos = bytes([clrs]) + b''.join(struct.pack('BB', c + 1, 0x00) for c in range(clrs)) + bytes([pred, 0, 0])
    def seg(marker, body): return b'\xff' + bytes([marker]) + struct.pack('>H', len(body) + 2) + body
    return b'\xff\xd8' + seg(0xC3, sof) + seg(0xC4, dht) + seg(0xDA, sos) + scan + b'\xff\xd9'

# ---- Nikon compressed encoder -------------------------------------------

NIKON_TREE_14_LOSSLESS = ([0,1,4,3,1,1,1,1,1,2,0,0,0,0,0,0], [5,6,4,7,8,3,9,2,1,0,10,11,12,13,14])

def nikon_encode(plane, width, height, vpred):
    counts, symbols = NIKON_TREE_14_LOSSLESS
    codes = huff_codes(counts, symbols)
    bw_out = bytearray(); acc = 0; n = 0
    def put(value, nbits):
        nonlocal acc, n
        for i in range(nbits - 1, -1, -1):
            acc = (acc << 1) | ((value >> i) & 1); n += 1
            if n == 8: bw_out.append(acc); acc = 0; n = 0
    vp = [list(vpred[0]), list(vpred[1])]
    hp = [0, 0]
    for row in range(height):
        for col in range(width):
            v = plane[row * width + col]
            if col < 2:
                d = v - vp[row & 1][col]; vp[row & 1][col] = v
            else:
                d = v - hp[col & 1]
            hp[col & 1] = v
            a = abs(d); length = 0
            while a: length += 1; a >>= 1
            code, l = codes[length]       # symbol = len (shl 0)
            put(code, l)
            if length:
                bits = d if d > 0 else d + (1 << length) - 1
                put(bits, length)
    while n: put(1, 1)
    return bytes(bw_out)

# ---- builders -------------------------------------------------------------

def dng_common(width, height, spp, photometric, bits):
    ents = [
        (254, 4, [0]), (256, 4, [width]), (257, 4, [height]), (258, 3, [bits] * spp),
        (262, 3, [photometric]), (271, 2, b'Polajuice\0'), (272, 2, b'Synthetic\0'),
        (274, 3, [1]), (277, 3, [spp]),
    ]
    return ents

def color_tags():
    # XYZ->camera matrix: identity-ish with a tilt so the path is exercised
    cm = [(10000, 10000), (0, 10000), (0, 10000), (0, 10000), (10000, 10000), (0, 10000), (0, 10000), (0, 10000), (10000, 10000)]
    # XYZ->camera = XYZ->sRGB, so that the decoder's recipe (ColorMatrix x
    # sRGB->XYZ, rows normalized, inverted) composes to the identity and a
    # decoded pixel must equal the scene exactly; the matrix itself is far
    # from identity, so the arithmetic is exercised
    cm = [(int(round(v * 10000)), 10000) for v in
          (3.2406, -1.5372, -0.4986, -0.9689, 1.8758, 0.0415, 0.0557, -0.2040, 1.0570)]
    return [
        (50706, 1, [1, 4, 0, 0]),                                  # DNGVersion
        (50717, 3, [WHITE]),                                       # WhiteLevel (12-bit scale)
        (50721, 10, cm),                                           # ColorMatrix1
        (50728, 5, [(10000, int(WB[0] * 10000)), (10000, 10000), (10000, int(WB[2] * 10000))]),  # AsShotNeutral
        (50778, 3, [21]),                                          # CalibrationIlluminant1 = D65
    ]

def build_bayer_u16():
    plane = bayer_plane(12)
    data = b''.join(struct.pack('<H', v) for v in plane)
    ents = dng_common(W, H, 1, 32803, 16) + [
        (259, 3, [1]), (273, 4, [('blob', 'data')]), (278, 4, [H]), (279, 4, [len(data)]),
        (33421, 3, [2, 2]), (33422, 1, CFA), (50714, 4, [BLACK]),
    ] + color_tags()
    ents.sort(key=lambda e: e[0])
    return tiff(ents, {'data': data})

def build_bayer_lj92():
    plane = bayer_plane(12)
    tw, th = 32, 24
    tiles = []
    for ty in range(0, H, th):
        for tx in range(0, W, tw):
            samples = [plane[(ty + y) * W + tx + x] for y in range(th) for x in range(tw)]
            tiles.append(ljpeg_encode(samples, tw // 2, th, 2, 12, 1))
    blobs = {'t%d' % i: t for i, t in enumerate(tiles)}
    ents = dng_common(W, H, 1, 32803, 12) + [
        (259, 3, [7]), (322, 4, [tw]), (323, 4, [th]),
        (324, 4, [('blob', 't%d' % i) for i in range(len(tiles))]),
        (325, 4, [len(t) for t in tiles]),
        (33421, 3, [2, 2]), (33422, 1, CFA), (50714, 4, [BLACK]),
    ] + color_tags()
    ents.sort(key=lambda e: e[0])
    return tiff(ents, blobs)

def build_linear_lj92():
    pix = linear_pixels(12)
    enc = ljpeg_encode(pix, W, H, 3, 12, 6)
    ents = dng_common(W, H, 3, 34892, 12) + [
        (259, 3, [7]), (322, 4, [W]), (323, 4, [H]),
        (324, 4, [('blob', 't0')]), (325, 4, [len(enc)]),
        (50713, 3, [1, 1]), (50714, 4, [BLACK, BLACK, BLACK]),
    ] + color_tags()
    ents.sort(key=lambda e: e[0])
    return tiff(ents, {'t0': enc})

def build_nef():
    bits = 14
    plane = bayer_plane(bits, white=16383)   # the curve's last entry is white
    # the first two samples of each row pair are predicted from vpred,
    # which the encoder seeds with the actual first row values
    data = nikon_encode(plane, W, H, [[0, 0], [0, 0]])
    # makernote: "Nikon\0" 02 11 00 00 + TIFF header (big endian) + IFD
    csize = 1 << bits
    curve = struct.pack('>BB', 0x44, 0x40) + struct.pack('>HHHH', 0, 0, 0, 0) + struct.pack('>H', csize) \
          + b''.join(struct.pack('>H', i) for i in range(csize))
    wb = [(int(WB[0] * 256), 256), (int(WB[2] * 256), 256), (256, 256), (256, 256)]  # R B G G
    black14 = [BLACK << 2] * 4
    mn_entries = [
        (0x0c, 5, wb),
        (0x3d, 3, black14),
        (0x96, 7, curve),
    ]
    mn_tiff = tiff(mn_entries, {}, big_endian=True)
    makernote = b'Nikon\0' + bytes([2, 0x11, 0, 0]) + mn_tiff
    # EXIF IFD holds the makernote; raw sub-IFD holds the image
    exif_ifd = tiff([(37500, 7, makernote)], {}, big_endian=True)
    sub_entries = [
        (254, 4, [0]), (256, 4, [W]), (257, 4, [H]), (258, 3, [bits]), (259, 3, [34713]),
        (262, 3, [32803]), (273, 4, [('blob', 'data')]), (277, 3, [1]), (278, 4, [H]),
        (279, 4, [len(data)]), (33421, 3, [2, 2]), (33422, 1, CFA),
    ]
    sub_tiff = tiff(sub_entries, {'data': data}, big_endian=True)
    # Compose: IFD0 at 8, then exif ifd, then sub ifd, with offsets rebased.
    # Simplest: build everything as independent TIFFs and splice their IFDs
    # by rebasing offsets - instead assemble by hand.
    return assemble_nef(bits, data, makernote)

def assemble_nef(bits, data, makernote):
    E = '>'
    def ifd_bytes(entries, base_off):
        """entries: (tag, type, bytes|list) with 'count' derived; values
        > 4 bytes placed right after the IFD. Returns (bytes, next_free)."""
        tsize = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 7: 1}
        packed = []
        for tag, typ, vals in entries:
            if isinstance(vals, bytes):
                data_b = vals; count = len(vals)
            elif typ == 5:
                data_b = b''.join(struct.pack(E + 'II', *v) for v in vals); count = len(vals)
            elif typ == 3:
                data_b = b''.join(struct.pack(E + 'H', v) for v in vals); count = len(vals)
            elif typ == 4:
                data_b = b''.join(struct.pack(E + 'I', v) for v in vals); count = len(vals)
            elif typ == 1:
                data_b = bytes(vals); count = len(vals)
            packed.append((tag, typ, count, data_b))
        ifd_len = 2 + 12 * len(packed) + 4
        vpos = base_off + ifd_len
        out = struct.pack(E + 'H', len(packed)); tail = b''
        for tag, typ, count, data_b in packed:
            if len(data_b) <= 4:
                out += struct.pack(E + 'HHI', tag, typ, count) + data_b.ljust(4, b'\0')
            else:
                out += struct.pack(E + 'HHII', tag, typ, count, vpos)
                pad = data_b + (b'\0' if len(data_b) & 1 else b'')
                tail += pad; vpos += len(pad)
        out += struct.pack(E + 'I', 0)
        return out + tail, vpos
    # layout plan: header(8) IFD0 | EXIF IFD | SUB IFD | raw data
    # IFD0 needs offsets of exif and sub ifds: compute sizes with dummies
    def build(exif_off, sub_off, data_off):
        ifd0, end0 = ifd_bytes([
            (254, 4, [1]), (256, 4, [160]), (257, 4, [120]), (271, 2, b'NIKON CORPORATION\0'),
            (272, 2, b'NIKON D610\0'), (274, 3, [1]), (330, 4, [sub_off]), (34665, 4, [exif_off]),
        ], 8)
        exif, end1 = ifd_bytes([(37500, 7, makernote)], exif_off)
        sub, end2 = ifd_bytes([
            (254, 4, [0]), (256, 4, [W]), (257, 4, [H]), (258, 3, [bits]), (259, 3, [34713]),
            (262, 3, [32803]), (273, 4, [data_off]), (277, 3, [1]), (278, 4, [H]),
            (279, 4, [len(data)]), (33421, 3, [2, 2]), (33422, 1, CFA),
        ], sub_off)
        return ifd0, end0, exif, end1, sub, end2
    ifd0, end0, exif, end1, sub, end2 = build(0, 0, 0)
    exif_off = end0; sub_off = exif_off + len(exif); data_off = sub_off + len(sub)
    ifd0, end0, exif, end1, sub, end2 = build(exif_off, sub_off, data_off)
    assert end0 == exif_off and end1 == sub_off and end2 == data_off
    return b'MM' + struct.pack(E + 'HI', 42, 8) + ifd0 + exif + sub + data

def build_png16():
    rows = b''
    for y in range(H):
        row = b'\0'
        for x in range(W):
            r, g, b = scene(x, y)
            for v in (r, g, b):
                row += struct.pack('>H', int(round(min(v, 1.0) * 65535)))
        rows += row
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 16, 2, 0, 0, 0)) \
         + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'tests/raw'
    os.makedirs(out, exist_ok=True)
    for name, fn in [('bayer_u16.dng', build_bayer_u16), ('bayer_lj92.dng', build_bayer_lj92),
                     ('linear_lj92.dng', build_linear_lj92), ('lossless14.nef', build_nef),
                     ('linear16.png', build_png16)]:
        data = fn()
        with open(os.path.join(out, name), 'wb') as f: f.write(data)
        print(name, len(data), 'bytes')
