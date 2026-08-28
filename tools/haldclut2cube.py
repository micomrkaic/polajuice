#!/usr/bin/env python3
"""Convert a HaldCLUT PNG (RawTherapee/G'MIC style) to a .cube 3D LUT.

Usage: haldclut2cube.py input.png [output.cube]

A level-N HaldCLUT is an N^3 x N^3 image encoding an N^2-point 3D LUT with
red varying fastest, then green, then blue, in row-major pixel order --
exactly the .cube data ordering, so the conversion is a straight dump.
"""
import sys
from PIL import Image

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".cube"

    im = Image.open(src).convert("RGB")
    w, h = im.size
    if w != h:
        sys.exit(f"{src}: not square ({w}x{h}); not a HaldCLUT")
    level = round(w ** (1.0 / 3.0))
    if level ** 3 != w:
        sys.exit(f"{src}: width {w} is not a perfect cube; not a HaldCLUT")
    size = level * level          # LUT lattice size, e.g. 64 for level 8

    # Large lattices (RawTherapee ships level-12 = 144) are resampled onto
    # a 64-point lattice: trilinear interpolation of the source lattice at
    # the target node positions, so both endpoints land exactly and
    # interior error is second-order on smooth data. (A naive stride
    # subsample cannot preserve the endpoints here: 143 intervals factor
    # as 11x13.) A 144^3 cube would be ~60MB of text per stock; 64^3 is
    # ~7MB and visually transparent under tetrahedral sampling.
    MAX = 64
    out_size = min(size, MAX)

    data = im.tobytes()           # RGB8, row-major == r-fastest LUT order
    def node(r_i, g_i, b_i):
        i = (b_i * size + g_i) * size + r_i
        return data[3 * i : 3 * i + 3]

    def sample(r_f, g_f, b_f):
        """Trilinear sample of the source lattice at float indices."""
        r0, g0, b0 = int(r_f), int(g_f), int(b_f)
        r1 = min(r0 + 1, size - 1)
        g1 = min(g0 + 1, size - 1)
        b1 = min(b0 + 1, size - 1)
        fr, fg, fb = r_f - r0, g_f - g0, b_f - b0
        out = [0.0, 0.0, 0.0]
        for c_i in range(3):
            c00 = node(r0, g0, b0)[c_i] * (1 - fr) + node(r1, g0, b0)[c_i] * fr
            c10 = node(r0, g1, b0)[c_i] * (1 - fr) + node(r1, g1, b0)[c_i] * fr
            c01 = node(r0, g0, b1)[c_i] * (1 - fr) + node(r1, g0, b1)[c_i] * fr
            c11 = node(r0, g1, b1)[c_i] * (1 - fr) + node(r1, g1, b1)[c_i] * fr
            out[c_i] = (c00 * (1 - fg) + c10 * fg) * (1 - fb) \
                     + (c01 * (1 - fg) + c11 * fg) * fb
        return out

    scale = (size - 1) / (out_size - 1)
    with open(dst, "w") as f:
        f.write(f'TITLE "{src}"\nLUT_3D_SIZE {out_size}\n'
                "DOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n")
        if out_size == size:
            for i in range(size ** 3):
                r, g, b = data[3 * i : 3 * i + 3]
                f.write(f"{r/255:.6f} {g/255:.6f} {b/255:.6f}\n")
        else:
            for b_i in range(out_size):
                for g_i in range(out_size):
                    for r_i in range(out_size):
                        r, g, b = sample(r_i * scale, g_i * scale, b_i * scale)
                        f.write(f"{r/255:.6f} {g/255:.6f} {b/255:.6f}\n")
    note = f" (resampled {size} -> {out_size})" if out_size != size else ""
    print(f"{dst}: LUT_3D_SIZE {out_size}{note}")

if __name__ == "__main__":
    main()
