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
    if size > 128:
        sys.exit(f"{src}: lattice {size} exceeds polajuice's 128 limit")

    data = im.tobytes()           # RGB8, row-major == r-fastest LUT order
    n = size ** 3
    with open(dst, "w") as f:
        f.write(f'TITLE "{src}"\nLUT_3D_SIZE {size}\n'
                "DOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n")
        for i in range(n):
            r, g, b = data[3 * i : 3 * i + 3]
            f.write(f"{r/255:.6f} {g/255:.6f} {b/255:.6f}\n")
    print(f"{dst}: LUT_3D_SIZE {size} ({n} entries)")

if __name__ == "__main__":
    main()
