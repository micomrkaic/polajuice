#!/usr/bin/env python3
"""build_guide.py - the figures, the checks and the PDF of docs/GUIDE.md.

The guide is one Markdown file. Every picture in it is made by a command
printed in it: this tool reads the `polajuice` lines out of the guide's
shell blocks, runs them on the six small photographs in docs/guide/photos
and files the results under docs/guide/img, so the text and the pictures
cannot drift apart. It also draws the diagrams (SVG), turns the Markdown
into HTML and has a headless Chrome print that to docs/GUIDE.pdf.

    python3 tools/build_guide.py            figures, diagrams, HTML, PDF
    python3 tools/build_guide.py --check    run every command in a scratch
                                            folder and check the guide
                                            against 'polajuice --help';
                                            writes nothing ('make check')
    python3 tools/build_guide.py --no-pdf   everything but the PDF

Python 3 standard library only. The PDF needs Chrome or Chromium
(CHROME=/path/to/chrome overrides the search); without one the HTML is
left at docs/GUIDE.html to print from any browser.

The figures are saved at JPEG quality 84 instead of the default 92 (it
halves the guide's size; the guide says so), unless the command sets a
quality itself. They are rendered with no film library in reach, so they come out
the same on every machine: a camera's canonical film is replaced by the
built-in color engine, exactly as on a fresh checkout. Commands that name
a library film (-f kodak_portra_400) are run against stand-in cubes, to
prove they parse; none of the guide's pictures comes from them.
"""
import html
import math
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
GUIDE = os.path.join(DOCS, "GUIDE.md")
PHOTOS = os.path.join(DOCS, "guide", "photos")
IMG = os.path.join(DOCS, "guide", "img")
POLAJUICE = os.path.join(ROOT, "polajuice")
SUPERJUICE = os.path.join(ROOT, "superjuice")

# the reader's own files in the guide's examples, and what stands in for
# them when the commands are run here
STAND_INS = {
    "photo.jpg": os.path.join(PHOTOS, "cat.jpg"),
    "DSC_0412.NEF": os.path.join(ROOT, "tests", "raw", "lossless14.nef"),
    "IMG_0033.dng": os.path.join(ROOT, "tests", "raw", "bayer_u16.dng"),
    "scan.png": os.path.join(ROOT, "tests", "raw", "linear16.png"),
}
# where a library film named in the guide would live (its pack decides
# the process the cameras check)
FILM_PACKS = {
    "kodak_portra_400": "negative_new", "kodak_portra_160": "negative_new",
    "kodak_portra_800": "negative_new", "fuji_superia_800": "negative_new",
    "kodak_kodachrome_64": "colorslide", "fuji_provia_100f": "colorslide",
    "fuji_velvia_50": "colorslide", "kodak_ektachrome_100_vs": "colorslide",
    "ilford_hp_5_plus_400": "bw", "kodak_tri-x_400": "bw", "ilford_delta_3200": "bw",
    "polaroid_px-680": "instant_consumer", "polaroid_px-70": "instant_consumer",
}
FIGURE_QUALITY = 84     # JPEG quality of the guide's own pictures (the program's default is 92)
IDENTITY_CUBE = ("TITLE \"stand-in\"\nLUT_3D_SIZE 2\n"
                 "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n")


def fail(msg):
    print("build_guide: " + msg, file=sys.stderr)
    sys.exit(1)


# --------------------------------------------------------------- the guide

def read_guide():
    with open(GUIDE, encoding="utf-8") as f:
        return f.read()


def shell_lines(md):
    """The command lines of the guide's ```sh blocks, continuations joined."""
    out, inside, pending = [], False, ""
    for line in md.split("\n"):
        if line.startswith("```"):
            inside = not inside and line.strip() in ("```sh", "```bash")
            pending = ""
            continue
        if not inside:
            continue
        if pending:
            line = pending + " " + line.strip()
            pending = ""
        if line.endswith("\\"):
            pending = line[:-1].rstrip()
            continue
        out.append(line)
    return out


def image_refs(md):
    return re.findall(r"!\[[^\]]*\]\(([^)\s]+)\)", md)


def runnable(line):
    """A polajuice line this tool can run unattended."""
    if not line.startswith("polajuice "):
        return False
    words = line.split()
    if words[1] in ("serve",) or "--install" in words:
        return False
    return not any(ch in line for ch in "~*$|><;&")


def output_of(words):
    for i, w in enumerate(words):
        if w in ("-o", "--output") and i + 1 < len(words):
            return words[i + 1]
    return None


def run_commands(md, work, quiet=True):
    """Run every runnable command of the guide in 'work'. Returns the list
    of (line, output file or None)."""
    for name in sorted(os.listdir(PHOTOS)):
        shutil.copy(os.path.join(PHOTOS, name), os.path.join(work, name))
    for name, src in STAND_INS.items():
        shutil.copy(src, os.path.join(work, name))
    with open(os.path.join(work, "my-look.cube"), "w") as f:
        f.write(IDENTITY_CUBE)
    empty = os.path.join(work, ".nofilms")
    films = os.path.join(work, ".films")
    os.makedirs(empty)
    for stem, pack in FILM_PACKS.items():
        os.makedirs(os.path.join(films, pack), exist_ok=True)
        with open(os.path.join(films, pack, stem + ".cube"), "w") as f:
            f.write(IDENTITY_CUBE)
    done = []
    refs = set(image_refs(md))
    for line in shell_lines(md):
        if not runnable(line):
            continue
        words = shlex.split(line)
        out = output_of(words)
        if (words[1] == "apply" and out and out.endswith(".jpg") and "--quality" not in words
                and ("guide/img/" + out) in refs):
            words += ["--quality", str(FIGURE_QUALITY)]   # the guide's pictures only: see the docstring
        uses_library = "-f" in words or "--film" in words or words[1] == "films" or "--vs" in words
        env = dict(os.environ)
        env["POLAJUICE_FILMS"] = films if uses_library else empty
        env["POLAJUICE_PRINTS"] = os.path.join(ROOT, "data", "prints")
        env.pop("POLAJUICE_HOME", None)
        r = subprocess.run([POLAJUICE] + words[1:], cwd=work, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            fail("this command from the guide fails:\n  %s\n%s" % (line, r.stderr.strip()))
        if not quiet:
            print("  " + line)
        done.append((line, output_of(words)))
    return done


# ---------------------------------------------------------------- diagrams
# Static SVG, drawn for print and for a page: a light surface of their own
# (so a dark-mode viewer does not swallow the ink), two series colors that
# stay apart for color-blind readers (blue and orange), and every series
# labelled on the mark, never by color alone.

INK, MUTED, GRID, SURFACE = "#0b0b0b", "#52514e", "#d9d8d2", "#fcfcfb"
BLUE, ORANGE = "#2a78d6", "#eb6834"
FONT = "DejaVu Sans,Helvetica,Arial,sans-serif"


class Svg:
    def __init__(self, w, h):
        self.w, self.h, self.out = w, h, []
        self.out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
                        'viewBox="0 0 %d %d" font-family="%s">' % (w, h, w, h, FONT))
        self.out.append('<rect width="100%%" height="100%%" fill="%s"/>' % SURFACE)

    def line(self, x1, y1, x2, y2, color=INK, width=1.0, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self.out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" '
                        'stroke-width="%.1f"%s/>' % (x1, y1, x2, y2, color, width, d))

    def rect(self, x, y, w, h, fill, stroke=None, rx=0):
        s = ' stroke="%s"' % stroke if stroke else ""
        self.out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%d" fill="%s"%s/>'
                        % (x, y, w, h, rx, fill, s))

    def path(self, pts, color=INK, width=2.0, dash=None, fill="none"):
        d = "M" + " L".join("%.1f %.1f" % p for p in pts)
        ds = ' stroke-dasharray="%s"' % dash if dash else ""
        self.out.append('<path d="%s" fill="%s" stroke="%s" stroke-width="%.1f" '
                        'stroke-linejoin="round"%s/>' % (d, fill, color, width, ds))

    def text(self, x, y, s, size=13, color=INK, anchor="start", weight="normal", italic=False):
        st = ' font-style="italic"' if italic else ""
        self.out.append('<text x="%.1f" y="%.1f" font-size="%d" fill="%s" text-anchor="%s" '
                        'font-weight="%s"%s>%s</text>' % (x, y, size, color, anchor, weight, st,
                                                           html.escape(s)))

    def arrow(self, x1, y1, x2, y2, color=MUTED):
        self.line(x1, y1, x2, y2, color, 1.2)
        a = math.atan2(y2 - y1, x2 - x1)
        for da in (2.6, -2.6):
            self.line(x2, y2, x2 + 8 * math.cos(a + da), y2 + 8 * math.sin(a + da), color, 1.2)

    def save(self, name):
        self.out.append("</svg>")
        with open(os.path.join(IMG, name), "w", encoding="utf-8") as f:
            f.write("\n".join(self.out) + "\n")


def srgb_encode(v):
    return 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055


def diagram_stops():
    """The stop: each step doubles the light; the three ways to take one."""
    s = Svg(760, 330)
    s.text(20, 26, "One stop = twice (or half) the light", 15, weight="bold")
    x0, dx, base = 130, 84, 170
    s.text(20, base - 4, "light", 12, MUTED)
    s.line(x0 - 34, base, x0 + 7 * dx + 34, base, GRID)
    for i in range(8):
        h = max(1.0, 110 * 2.0 ** (i - 7))          # to scale: each bar twice the last
        s.rect(x0 + i * dx - 22, base - h, 44, h, BLUE, rx=2 if h > 6 else 0)
        s.text(x0 + i * dx, base - h - 7, "×%s" % ["⅛", "¼", "½", "1", "2", "4", "8", "16"][i], 12, MUTED, "middle")
        s.text(x0 + i * dx, base + 20, "%+d" % (i - 3) if i != 3 else "0", 13, anchor="middle", weight="bold")
    s.text(20, base + 20, "stops", 12, MUTED)
    rows = [("aperture", ["f/16", "f/11", "f/8", "f/5.6", "f/4", "f/2.8", "f/2", "f/1.4"]),
            ("shutter", ["1/1000", "1/500", "1/250", "1/125", "1/60", "1/30", "1/15", "1/8"]),
            ("ISO", ["100", "200", "400", "800", "1600", "3200", "6400", "12800"])]
    for r, (name, vals) in enumerate(rows):
        yy = base + 56 + r * 30
        s.line(20, yy - 19, 740, yy - 19, GRID)
        s.text(20, yy, name, 12, weight="bold")
        for i, v in enumerate(vals):
            s.text(x0 + i * dx, yy, v, 12, anchor="middle")
    s.text(20, 316, "The bars are to scale. One column to the right in any row adds one stop; "
           "right in one row and left in another is the same exposure.", 11, MUTED)
    s.save("stops.svg")


def diagram_curve():
    """A film's characteristic curve: toe, straight line, shoulder."""
    s = Svg(760, 380)
    L, T, W, H = 70, 40, 420, 280
    s.text(20, 24, "The characteristic curve", 15, weight="bold")
    for i in range(9):
        x = L + W * i / 8
        s.line(x, T, x, T + H, GRID)
        s.text(x, T + H + 18, "%+d" % (i - 4) if i != 4 else "0", 11, MUTED, "middle")
    for j in range(5):
        s.line(L, T + H * j / 4, L + W, T + H * j / 4, GRID)
    s.text(L + W / 2, T + H + 40, "scene brightness, stops from middle gray  →", 12, MUTED, "middle")
    s.text(L - 12, T + 4, "white", 11, MUTED, "end")
    s.text(L - 12, T + H, "black", 11, MUTED, "end")

    def film(x, slope):
        return 1 / (1 + math.exp(-slope * x))
    for slope, color, dash, lab in ((0.95, BLUE, None, "normal"), (1.6, ORANGE, "7 4", "more contrast")):
        pts = [(L + W * (x + 4) / 8, T + H * (1 - film(x, slope))) for x in [k / 10 - 4 for k in range(81)]]
        s.path(pts, color, 2.2, dash)
    s.path([(L, T + H), (L + W, T)], MUTED, 1.0, "2 4")
    s.text(L + 12, T + H * 0.50, "toe", 13, weight="bold")
    s.text(L + 12, T + H * 0.50 + 16, "shadows crowd", 11, MUTED)
    s.text(L + 12, T + H * 0.50 + 30, "together", 11, MUTED)
    s.arrow(L + 30, T + H * 0.50 + 38, L + W * 0.10, T + H * 0.90)
    s.text(L + W * 0.57, T + H * 0.70, "straight part", 13, weight="bold")
    s.text(L + W * 0.57, T + H * 0.70 + 16, "its slope is the contrast", 11, MUTED)
    s.arrow(L + W * 0.56, T + H * 0.66, L + W * 0.52, T + H * 0.52)
    s.text(L + W * 0.70, T + H * 0.44, "shoulder", 13, weight="bold")
    s.text(L + W * 0.70, T + H * 0.44 + 16, "highlights roll off", 11, MUTED)
    s.arrow(L + W * 0.80, T + H * 0.39, L + W * 0.86, T + H * 0.11)
    x = 520
    s.line(x, 70, x + 40, 70, BLUE, 2.2)
    s.text(x + 48, 74, "normal development", 12)
    s.line(x, 96, x + 40, 96, ORANGE, 2.2, "7 4")
    s.text(x + 48, 100, "steeper: a push, or", 12)
    s.text(x + 48, 116, "--contrast above 0", 12)
    s.line(x, 138, x + 40, 138, MUTED, 1.0, "2 4")
    s.text(x + 48, 142, "a straight copy", 12)
    for i, t in enumerate(["Read it left to right: how bright",
                           "the scene was, and how bright the",
                           "film made it. Where the curve is",
                           "steep, tones are pulled apart;",
                           "where it is flat, they merge.",
                           "Digital sensors have no shoulder:",
                           "they are a straight line that",
                           "stops dead at white."]):
        s.text(x, 182 + i * 18, t, 12, MUTED)
    s.save("curve.svg")


def diagram_bayer():
    """The color mosaic over a sensor, and what demosaicing fills in."""
    s = Svg(760, 300)
    s.text(20, 24, "One color per pixel: the Bayer mosaic", 15, weight="bold")
    cell, x0, y0 = 38, 30, 50
    fills = {"R": "#f1c4b3", "G": "#d9e4d2", "B": "#b7d3f6"}
    for r in range(6):
        for c in range(6):
            k = "G" if (r + c) % 2 == 0 else ("R" if r % 2 == 0 else "B")
            s.rect(x0 + c * cell, y0 + r * cell, cell - 2, cell - 2, fills[k], MUTED)
            s.text(x0 + c * cell + cell / 2 - 1, y0 + r * cell + cell / 2 + 5, k, 14, anchor="middle",
                   weight="bold")
    s.text(x0, y0 + 6 * cell + 16, "what the sensor records: one number per pixel", 11, MUTED)
    s.arrow(275, 150, 335, 150)
    s.text(305, 138, "demosaic", 11, MUTED, "middle")
    x1 = 350
    for r in range(6):
        for c in range(6):
            s.rect(x1 + c * cell, y0 + r * cell, cell - 2, cell - 2, "#ececea", MUTED)
            s.text(x1 + c * cell + cell / 2 - 1, y0 + r * cell + cell / 2 + 4, "RGB", 10, anchor="middle")
    s.text(x1, y0 + 6 * cell + 16, "what you get: three per pixel, two of them estimated", 11, MUTED)
    for i, t in enumerate(["Half the pixels see", "green, a quarter red,", "a quarter blue.",
                           "", "The two missing colors", "at each pixel are", "estimated from its",
                           "neighbors. Fine detail", "and hard edges are", "where the guess shows:",
                           "color fringes and moiré."]):
        s.text(596, 66 + i * 18, t, 12, MUTED)
    s.save("bayer.svg")


def diagram_gamma():
    """Linear light against display encoding."""
    s = Svg(760, 380)
    L, T, W, H = 70, 62, 330, 270
    s.text(20, 24, "Linear light and its encoding", 15, weight="bold")
    for i in range(5):
        s.line(L + W * i / 4, T, L + W * i / 4, T + H, GRID)
        s.line(L, T + H * i / 4, L + W, T + H * i / 4, GRID)
        s.text(L + W * i / 4, T + H + 18, "%d%%" % (25 * i), 11, MUTED, "middle")
        s.text(L - 8, T + H - H * i / 4 + 4, "%d" % round(255 * i / 4), 11, MUTED, "end")
    s.text(L + W / 2, T + H + 40, "light (linear): fraction of white  →", 12, MUTED, "middle")
    s.text(L - 50, T - 12, "stored value (8 bits)", 12, MUTED)
    s.path([(L + W * v / 100, T + H * (1 - srgb_encode(v / 100))) for v in range(101)], BLUE, 2.2)
    s.path([(L, T + H), (L + W, T)], MUTED, 1.0, "2 4")
    s.text(L + W * 0.30, T + H * 0.20, "sRGB encoding", 12, BLUE, weight="bold")
    s.text(L + W * 0.62, T + H * 0.56, "stored = light", 11, MUTED)
    g = srgb_encode(0.18)
    gx, gy = L + W * 0.18, T + H * (1 - g)
    s.line(gx, T + H, gx, gy, ORANGE, 1.4, "4 3")
    s.line(L, gy, gx, gy, ORANGE, 1.4, "4 3")
    s.out.append('<circle cx="%.1f" cy="%.1f" r="5" fill="%s" stroke="%s" stroke-width="2"/>'
                 % (gx, gy, ORANGE, SURFACE))
    s.text(gx + 10, gy + 18, "middle gray: 18% of the light,", 11)
    s.text(gx + 10, gy + 33, "stored as %d of 255" % round(255 * g), 11)
    x = 430
    s.text(x, 60, "Stops down from white, and the share of the", 12, weight="bold")
    s.text(x, 76, "256 code values each one gets", 12, weight="bold")
    s.text(x + 70, 100, "stored linearly", 11, MUTED)
    s.text(x + 215, 100, "sRGB-encoded", 11, MUTED)
    for i in range(6):
        hi, lo = 2.0 ** -i, 2.0 ** -(i + 1)
        lin = round(255 * hi) - round(255 * lo)
        enc = round(255 * srgb_encode(hi)) - round(255 * srgb_encode(lo))
        yy = 122 + i * 30
        s.text(x, yy + 4, "stop %d" % (i + 1), 12)
        s.rect(x + 70, yy - 8, lin * 0.8, 14, MUTED, rx=2)
        s.text(x + 74 + lin * 0.8, yy + 4, str(lin), 11)
        s.rect(x + 215, yy - 8, enc * 0.8, 14, BLUE, rx=2)
        s.text(x + 219 + enc * 0.8, yy + 4, str(enc), 11)
    s.text(x, 320, "Stored linearly, half the values go to the brightest", 11, MUTED)
    s.text(x, 335, "stop and the shadows starve. The encoding shares", 11, MUTED)
    s.text(x, 350, "them out the way the eye needs them.", 11, MUTED)
    s.save("gamma.svg")


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(None, 4)
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][-w * h * 3:]


def diagram_histogram(work):
    """Three histograms of the same picture at three exposures."""
    env = dict(os.environ)
    env["POLAJUICE_FILMS"] = os.path.join(work, ".nofilms")
    panels = []
    for ev, title in ((-2, "two stops under (--ev -2)"), (0, "as shot"), (2, "two stops over (--ev 2)")):
        out = os.path.join(work, ".hist%+d.ppm" % ev)
        r = subprocess.run([POLAJUICE, "apply", "street.jpg", "-c", "35mm-slide", "--no-film",
                            "--grainamount", "0", "--halation", "0", "--ev", str(ev), "-o", out],
                           cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            fail("histogram render failed: " + r.stderr)
        w, h, px = read_ppm(out)
        bins = [0] * 64
        for i in range(0, len(px), 3):
            y = (px[i] * 54 + px[i + 1] * 183 + px[i + 2] * 19) >> 8
            bins[y >> 2] += 1
        n = w * h
        panels.append((title, [b / n for b in bins]))
    s = Svg(760, 264)
    s.text(20, 24, "Reading a histogram: how many pixels at each brightness", 15, weight="bold")
    pw, ph, top = 220, 130, 60
    peak = max(sorted(v for _, b in panels for v in b)[-4], 1e-6)
    for k, (title, bins) in enumerate(panels):
        x0 = 20 + k * 250
        s.rect(x0, top, pw, ph, "#f3f2ee", GRID)
        for i, v in enumerate(bins):
            hh = min(ph, ph * v / peak)
            s.rect(x0 + i * pw / 64, top + ph - hh, pw / 64 - 0.6, hh, BLUE)
        s.text(x0, top - 10, title, 12, weight="bold")
        s.text(x0, top + ph + 16, "black", 11, MUTED)
        s.text(x0 + pw, top + ph + 16, "white", 11, MUTED, "end")
        crushed, blown = sum(bins[:2]), sum(bins[-2:])
        note = ("piled at the left: %d%% is near black" % round(100 * crushed) if k == 0 else
                "piled at the right: %d%% is near white" % round(100 * blown) if k == 2 else
                "spread out, little at either wall")
        s.text(x0, top + ph + 36, note, 11, MUTED)
    s.text(20, 252, "A pile against a wall is detail that is gone: no later adjustment brings it back.", 11, MUTED)
    s.save("histogram.svg")


def diagram_tone_eq(work):
    """The tone equalizer: the bands as bells, and what they do to the curve."""
    bands = [(1, 1.2, -4.0, 1.5), (4, -0.7, 2.0, 1.0)]          # band, lift, center, width
    args = []
    for i, g, at, w in bands:
        args += ["--tone%d" % i, str(g), "--tone%dat" % i, str(at), "--tone%dwidth" % i, str(w)]
    env = dict(os.environ)
    env["POLAJUICE_FILMS"] = os.path.join(work, ".nofilms")
    r = subprocess.run([POLAJUICE, "curves", "-c", "35mm-slide", "--no-film", "--table"] + args,
                       cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        fail("tone curve table failed: " + r.stderr)
    rows = []
    for line in r.stdout.split("\n"):
        f = line.split()
        if len(f) == 3 and f[0][0] in "+-":
            try:
                rows.append((float(f[0]), float(f[1]), float(f[2])))
            except ValueError:
                pass
    if len(rows) < 10:
        fail("tone curve table not understood")
    s = Svg(760, 430)
    s.text(20, 24, "The tone equalizer: two bands, and what they do to the curve", 15, weight="bold")
    L, W = 70, 430
    x_lo, x_hi = -9.0, 2.5
    X = lambda x: L + W * (x - x_lo) / (x_hi - x_lo)
    # upper panel: the curve
    T, H = 44, 210
    for x in range(-9, 3):
        s.line(X(x), T, X(x), T + H, "#b9b8b0" if x == 0 else GRID)
    for j in range(5):
        s.line(L, T + H * j / 4, L + W, T + H * j / 4, GRID)
    s.text(L - 8, T + 6, "white", 11, MUTED, "end")
    s.text(L - 8, T + H, "black", 11, MUTED, "end")
    s.path([(X(x), T + H * (1 - a)) for x, a, b in rows], MUTED, 1.4, "5 4")
    s.path([(X(x), T + H * (1 - b)) for x, a, b in rows], BLUE, 2.2)

    def level(x, col):
        for k in range(1, len(rows)):
            if rows[k][0] >= x:
                t = (x - rows[k - 1][0]) / (rows[k][0] - rows[k - 1][0])
                return rows[k - 1][col] + t * (rows[k][col] - rows[k - 1][col])
        return rows[-1][col]
    for i, g, at, w in bands:
        cx, cy = X(at), T + H * (1 - level(at, 2))
        s.line(max(L, X(at - w)), cy, min(L + W, X(at + w)), cy, INK, 1.2)
        s.out.append('<circle cx="%.1f" cy="%.1f" r="9" fill="%s" stroke="%s" stroke-width="1.6"/>' % (cx, cy, SURFACE, INK))
        s.text(cx, cy + 4, str(i), 11, anchor="middle", weight="bold")
    # lower panel: the bells, in stops of lift
    T2, H2 = 300, 80
    zero = T2 + H2 / 2
    for x in range(-9, 3):
        s.line(X(x), T2, X(x), T2 + H2, "#b9b8b0" if x == 0 else GRID)
        if x % 2 == 0 or x == 0:
            s.text(X(x), T2 + H2 + 16, "%+d" % x if x else "0", 11, MUTED, "middle")
    s.line(L, zero, L + W, zero, MUTED, 1.0)
    for v in (1, -1):
        s.text(L - 8, zero - v * H2 / 3 + 4, "%+d" % v, 11, MUTED, "end")
    s.text(L - 8, zero + 4, "0", 11, MUTED, "end")
    xs = [x_lo + (x_hi - x_lo) * k / 230 for k in range(231)]
    bell = lambda x: sum(g * math.exp(-0.5 * ((x - at) / w) ** 2) for _, g, at, w in bands)
    s.path([(X(x), zero - bell(x) * H2 / 3) for x in xs], ORANGE, 2.0)
    for i, g, at, w in bands:
        s.text(X(at), zero - g * H2 / 3 + (-8 if g > 0 else 16), "band %d" % i, 11, anchor="middle", weight="bold")
    s.text(L + W / 2, T2 + H2 + 34, "scene brightness in the file, stops from middle gray  →", 12, MUTED, "middle")
    s.text(L, T2 - 8, "what the bands add, in stops", 11, MUTED)
    # the legend and the reading, at the right
    x = 525
    s.line(x, 62, x + 36, 62, MUTED, 1.4, "5 4")
    s.text(x + 44, 66, "the film alone", 12)
    s.line(x, 86, x + 36, 86, BLUE, 2.2)
    s.text(x + 44, 90, "with the two bands", 12)
    s.line(x, 110, x + 36, 110, ORANGE, 2.0)
    s.text(x + 44, 114, "the bands themselves", 12)
    for k, t in enumerate(["Band 1 lifts the deep shadows:", "+1.2 stops, centered four stops",
                           "under middle gray, 1.5 stops", "wide. Band 4 cuts the highlights:",
                           "-0.7 stops, two stops over, one", "wide.", "",
                           "Each handle sits on the curve at", "its band's center; the bar through",
                           "it is the band's width. Between", "the bands the curve is untouched."]):
        s.text(x, 150 + k * 17, t, 11, MUTED)
    s.text(x, 356, "--tone1 1.2 --tone1width 1.5", 10)
    s.text(x, 371, "--tone4 -0.7", 10)
    s.save("tone-eq.svg")


def diagram_pipeline():
    """The order polajuice works in."""
    s = Svg(760, 320)
    s.text(20, 24, "The order of work: light, camera, film, darkroom", 15, weight="bold")
    groups = [
        ("1  The frame", ["read: JPEG, PNG, raw…", "denoise, sharpen", "straighten, keystone", "distortion, corners", "trim, frame shape"]),
        ("2  The scene", ["white balance", "grad, radial filter", "exposure (--ev)", "tone equalizer; black,", "shadows, contrast; filter"]),
        ("3  The camera", ["flash", "lens softness", "vignette", "halation"]),
        ("4  The film", ["color and tone:", "the film stock (-f),", "or built-in color", "edge effects", "silver grain"]),
        ("5  The darkroom", ["print stock", "push, pull, cross", "age", "classic grain", "instant-print border"]),
    ]
    bw, gap, bh, y = 130, 17, 172, 50
    for i, (title, items) in enumerate(groups):
        x = 20 + i * (bw + gap)
        s.rect(x, y, bw, bh, "#f3f2ee", GRID, rx=6)
        s.rect(x, y, bw, 30, "#184f95", rx=6)
        s.text(x + 10, y + 20, title, 13, "#ffffff", weight="bold")
        for j, t in enumerate(items):
            s.text(x + 10, y + 56 + j * 22, t, 11)
        if i < 4:
            s.arrow(x + bw + 2, y + 15, x + bw + gap - 2, y + 15, INK)
    s.text(20, 252, "Everything in 1 and 2 happens to the light before it reaches the film, so the film responds to it", 11, MUTED)
    s.text(20, 268, "as it would have in the camera. Everything in 5 happens to the developed film.", 11, MUTED)
    s.text(20, 300, "The order of the options on the command line does not matter: this order is fixed.", 11, MUTED)
    s.save("pipeline.svg")


def diagram_formats():
    """The format crops the cameras cut from a 3:2 frame."""
    s = Svg(760, 250)
    s.text(20, 24, "What --crop camera keeps of a 3:2 frame", 15, weight="bold")
    shapes = [("35mm, bw-35", 1.5, "the whole frame"), ("super8", 1.36, "gate 1.36:1"),
              ("technicolor", 1.38, "Academy 1.38:1"), ("toy-camera-120", 1.0, "square"),
              ("polaroid-600", 1.03, "print 1.03:1 + border")]
    fw, fh = 126, 84
    for i, (name, aspect, note) in enumerate(shapes):
        x, y = 20 + i * 148, 60
        s.rect(x, y, fw, fh, "#e4e3dd", MUTED)
        kw = min(fw, fh * aspect)
        s.rect(x + (fw - kw) / 2, y, kw, fh, "#9ec5f4", BLUE)
        s.text(x + fw / 2, y + fh / 2 + 4, "kept", 11, anchor="middle")
        s.text(x, y + fh + 20, name, 12, weight="bold")
        s.text(x, y + fh + 36, note, 11, MUTED)
    s.text(20, 222, "The gray strips are cut away. --crop full keeps them: the look is applied to the whole picture,", 11, MUTED)
    s.text(20, 238, "and an instant camera puts its border around all of it.", 11, MUTED)
    s.save("formats.svg")


DIAGRAMS = {
    "stops.svg": diagram_stops, "curve.svg": diagram_curve, "bayer.svg": diagram_bayer,
    "gamma.svg": diagram_gamma, "pipeline.svg": diagram_pipeline, "formats.svg": diagram_formats,
    "histogram.svg": None,   # needs renders: drawn with the work folder
    "tone-eq.svg": None,     # needs the engine's curve: drawn with the work folder
}


# ---------------------------------------------------- Markdown to HTML
# The subset the guide is written in: headings, paragraphs, fenced code,
# pipe tables, flat lists, block quotes, rules; inline code, bold, italic,
# links and images. No raw HTML. Heading ids are GitHub's, so the same
# contents links work there and here.

def slug(text):
    text = re.sub(r"`", "", text.lower())
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return text.strip().replace(" ", "-")


def inline(text):
    codes = []

    def stash(m):
        # a command may wrap at its spaces, never inside an option or a file name
        words = ['<span class="nw">%s</span>' % html.escape(w, quote=False) for w in m.group(1).split(" ")]
        codes.append("<code>%s</code>" % " ".join(words))
        return "\x00%d\x00" % (len(codes) - 1)
    text = re.sub(r"`([^`]+)`", stash, text)
    text = re.sub(r"<(https?://[^>\s]+)>", r"[\1](\1)", text)
    text = html.escape(text, quote=False)
    text = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", r'<img alt="\1" src="\2">', text)
    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", r'<a href="\2">\1</a>', text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", text)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], text)


def table_cells(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in re.split(r"(?<!\\)\|", line)]


def markdown_to_html(md):
    lines = md.split("\n")
    out, i, n = [], 0, len(lines)
    while i < n:
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        if line.startswith("```"):
            j = i + 1
            while j < n and not lines[j].startswith("```"):
                j += 1
            out.append("<pre><code>%s</code></pre>" % html.escape("\n".join(lines[i + 1:j]), quote=False))
            i = j + 1
            continue
        m = re.match(r"(#{1,4}) (.*)", line)
        if m:
            level, text = len(m.group(1)), m.group(2).strip()
            out.append('<h%d id="%s">%s</h%d>' % (level, slug(text), inline(text), level))
            i += 1
            continue
        if re.match(r"-{3,}\s*$", line):
            out.append("<hr>")
            i += 1
            continue
        if line.lstrip().startswith("|") and i + 1 < n and re.match(r"\s*\|[\s:|-]+\|\s*$", lines[i + 1]):
            head = table_cells(line)
            aligns = ["center" if c.startswith(":") and c.endswith(":") else
                      "right" if c.endswith(":") else "left" for c in table_cells(lines[i + 1])]
            rows, j = [], i + 2
            while j < n and lines[j].lstrip().startswith("|"):
                rows.append(table_cells(lines[j]))
                j += 1
            figs = any(c.startswith("![") for r in rows for c in r)
            out.append('<table class="%s cols%d">' % ("figs" if figs else "data", len(head)))
            if any(head):
                out.append("<thead><tr>%s</tr></thead>" % "".join(
                    '<th style="text-align:%s">%s</th>' % (a, inline(c)) for c, a in zip(head, aligns)))
            out.append("<tbody>")
            for r in rows:
                out.append("<tr>%s</tr>" % "".join(
                    '<td style="text-align:%s">%s</td>' % (a, inline(c)) for c, a in zip(r, aligns)))
            out.append("</tbody></table>")
            i = j
            continue
        if line.startswith(">"):
            block = []
            while i < n and lines[i].startswith(">"):
                block.append(lines[i][1:].lstrip())
                i += 1
            out.append("<blockquote>%s</blockquote>" % markdown_to_html("\n".join(block)))
            continue
        m = re.match(r"(\d+\.|-) +(.*)", line)
        if m:
            ordered = m.group(1) != "-"
            items = []
            while i < n:
                m = re.match(r"(\d+\.|-) +(.*)", lines[i])
                if m and (m.group(1) != "-") == ordered:
                    items.append(m.group(2))
                    i += 1
                elif lines[i].startswith("  ") and lines[i].strip() and items:
                    items[-1] += " " + lines[i].strip()
                    i += 1
                else:
                    break
            tag = "ol" if ordered else "ul"
            out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % inline(t) for t in items), tag))
            continue
        para = []
        while i < n and lines[i].strip() and not re.match(r"(```|#{1,4} |>|\||-{3,}\s*$|(\d+\.|-) )", lines[i]):
            para.append(lines[i].strip())
            i += 1
        if not para:      # a line that only looked like a block start
            para.append(lines[i].strip())
            i += 1
        text = " ".join(para)
        if re.fullmatch(r"!\[[^\]]*\]\([^)\s]+\)", text):
            out.append('<p class="figure">%s</p>' % inline(text))
        else:
            out.append("<p>%s</p>" % inline(text))
    return "\n".join(out)


CSS = """
@page { size: A4; margin: 20mm 18mm 22mm 18mm;
        @bottom-center { content: counter(page); font: 9pt "Fira Sans","Helvetica Neue",Arial,sans-serif; color: #777; } }
@page :first { @bottom-center { content: ""; } }
html { font: 10.4pt/1.5 Charter,"Bitstream Charter",Georgia,"DejaVu Serif",serif; color: #111; }
body { margin: 0; max-width: 174mm; }
@media screen { body { margin: 2em auto; padding: 0 1em; } }
h1, h2, h3, h4, th, .toc { font-family: "Fira Sans","Helvetica Neue",Helvetica,Arial,"DejaVu Sans",sans-serif; }
h1 { font-size: 22pt; line-height: 1.15; margin: 0 0 .5em; break-before: page; }
h1:first-of-type { break-before: avoid; font-size: 30pt; margin-top: 30mm; }
h2 { font-size: 14.5pt; margin: 1.5em 0 .4em; break-after: avoid; border-bottom: .6pt solid #bbb; padding-bottom: .15em; }
h3 { font-size: 11.5pt; margin: 1.3em 0 .3em; break-after: avoid; }
h4 { font-size: 10.4pt; margin: 1.1em 0 .2em; break-after: avoid; }
p { margin: .55em 0; orphans: 3; widows: 3; }
a { color: #184f95; text-decoration: none; }
code { font: 8.8pt "DejaVu Sans Mono",Menlo,Consolas,monospace; background: #f1f0ec; padding: .05em .25em; border-radius: 2px; }
pre { background: #f1f0ec; border-left: 2.5pt solid #2a78d6; padding: .6em .8em; margin: .7em 0; break-inside: avoid;
      white-space: pre-wrap; overflow-wrap: anywhere; }
pre code { background: none; padding: 0; font-size: 8.4pt; line-height: 1.45; }
blockquote { margin: .8em 0; padding: .45em .9em; background: #f6f5f0; border-left: 2.5pt solid #52514e; break-inside: avoid; }
blockquote p { margin: .3em 0; }
ul, ol { margin: .5em 0; padding-left: 1.5em; }
li { margin: .2em 0; }
hr { border: 0; border-top: .6pt solid #bbb; margin: 1.4em 0; }
table { border-collapse: collapse; margin: .8em 0; width: 100%; break-inside: avoid; }
table.data { font-size: 9.2pt; line-height: 1.35; }
table.data th { text-align: left; border-bottom: 1pt solid #333; padding: .3em .5em; }
table.data td { border-bottom: .4pt solid #ccc; padding: .3em .5em; vertical-align: top; }
table.data code { font-size: 8.2pt; }
.nw { white-space: nowrap; }
table.figs { table-layout: fixed; }
table.figs th { font: 8pt "DejaVu Sans Mono",Menlo,monospace; font-weight: normal; color: #333; padding: 0 3pt 3pt; vertical-align: bottom; }
table.figs th code { background: none; padding: 0; font-size: 8pt; }
table.figs td { padding: 0 3pt 3pt; vertical-align: top; font-size: 8.6pt; color: #444; line-height: 1.3; }
table.figs img { width: 100%; height: auto; display: block; }
table.figs.cols1 { width: 55%; margin-left: auto; margin-right: auto; }
table.figs.cols2 { width: 74%; margin-left: auto; margin-right: auto; }
p.figure { text-align: center; break-inside: avoid; }
p.figure img { max-width: 100%; height: auto; }
"""


def build_html(md, version):
    body = markdown_to_html(md)
    return ("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
            "<title>The polajuice guide</title>\n"
            "<meta name=\"generator\" content=\"polajuice %s tools/build_guide.py\">\n"
            "<style>%s</style></head>\n<body>\n%s\n</body></html>\n" % (version, CSS, body))


def find_chrome():
    cands = [os.environ.get("CHROME", "")]
    for name in ("chromium", "chromium-browser", "google-chrome", "google-chrome-stable", "chrome",
                 "microsoft-edge", "brave-browser"):
        cands.append(shutil.which(name) or "")
    cands += ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
              "/Applications/Chromium.app/Contents/MacOS/Chromium",
              "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"]
    for c in cands:
        if c and os.path.exists(c):
            return c
    return None


def build_pdf(html_path, pdf_path):
    chrome = find_chrome()
    if not chrome:
        print("build_guide: no Chrome or Chromium found (set CHROME=/path/to/chrome); "
              "docs/GUIDE.html is written - print it to PDF from any browser")
        return False
    with tempfile.TemporaryDirectory() as profile:
        r = subprocess.run([chrome, "--headless=new", "--no-sandbox", "--disable-gpu",
                            "--user-data-dir=" + profile, "--no-pdf-header-footer",
                            "--generate-pdf-document-outline",
                            "--print-to-pdf=" + pdf_path, "file://" + html_path],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0 or not os.path.exists(pdf_path):
        fail("Chrome could not print the guide:\n" + r.stderr[-800:])
    return True


# ------------------------------------------------------------------ checks

def help_options(binary):
    r = subprocess.run([binary, "--help"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return set(re.findall(r"(?<![\w-])(--[a-z][a-z0-9-]*)", r.stdout))


def check_guide(md, produced):
    problems = []
    known = help_options(POLAJUICE)
    other = help_options(SUPERJUICE) | {"--rawtherapee", "--check", "--no-pdf", "--version"}
    # every option polajuice has is explained somewhere in the guide
    for opt in sorted(known - {"--help", "--preset", "--lut"}):
        if not re.search(r"(?<![\w-])%s(?![\w-])" % re.escape(opt), md):
            problems.append("option %s is in 'polajuice --help' but not in the guide" % opt)
    # and the guide names no option that does not exist
    code = " ".join(re.findall(r"`([^`\n]+)`", md)) + " " + " ".join(shell_lines(md))
    for opt in sorted(set(re.findall(r"(?<![\w-])(--[a-z][a-z0-9-]*)", code))):
        if opt not in known and opt not in other:
            problems.append("the guide names %s, which no tool here has" % opt)
    # every camera is named
    r = subprocess.run([POLAJUICE, "cameras"], stdout=subprocess.PIPE, text=True)
    for cam in re.findall(r"^(\S+) ", r.stdout, flags=re.M):
        if "`%s`" % cam not in md and cam not in md:
            problems.append("camera %s is not in the guide" % cam)
    # every picture is a photo, a diagram, or the output of a command in the guide
    outputs = {o for _, o in produced if o}
    for ref in image_refs(md):
        name = os.path.basename(ref)
        if ref.startswith("guide/photos/"):
            if not os.path.exists(os.path.join(PHOTOS, name)):
                problems.append("missing photo " + ref)
        elif ref.startswith("guide/img/"):
            if name not in DIAGRAMS and name not in outputs:
                problems.append("picture %s is made by no command in the guide" % ref)
        else:
            problems.append("picture outside docs/guide: " + ref)
    # every heading link resolves
    ids = {slug(m) for m in re.findall(r"^#{1,4} (.*)$", md, flags=re.M)}
    for target in re.findall(r"\]\(#([^)]+)\)", md):
        if target not in ids:
            problems.append("contents link to a heading that is not there: #" + target)
    return problems


def main():
    args = set(sys.argv[1:])
    if not os.path.exists(POLAJUICE):
        fail("build polajuice first (make)")
    md = read_guide()
    with open(os.path.join(ROOT, "VERSION")) as f:
        version = f.read().strip()
    with tempfile.TemporaryDirectory() as work:
        produced = run_commands(md, work, quiet="--check" in args)
        problems = check_guide(md, produced)
        if problems:
            fail("the guide and the program disagree:\n  " + "\n  ".join(problems))
        if "--check" in args:
            for ref in image_refs(md):
                if not os.path.exists(os.path.join(DOCS, ref)):
                    fail("picture %s is not built: run 'make guide'" % ref)
            print("guide: %d commands run, %d pictures accounted for, options and cameras all covered"
                  % (len(produced), len(image_refs(md))))
            return
        os.makedirs(IMG, exist_ok=True)
        wanted = {os.path.basename(r) for r in image_refs(md) if r.startswith("guide/img/")}
        for name in sorted(os.listdir(IMG)):
            if name not in wanted:
                os.remove(os.path.join(IMG, name))
        for name in sorted(wanted - set(DIAGRAMS)):
            shutil.copy(os.path.join(work, name), os.path.join(IMG, name))
        for name, fn in DIAGRAMS.items():
            if name in wanted and fn:
                fn()
        if "histogram.svg" in wanted:
            diagram_histogram(work)
        if "tone-eq.svg" in wanted:
            diagram_tone_eq(work)
    print("guide: %d pictures in docs/guide/img" % len(wanted))
    html_path = os.path.join(DOCS, "GUIDE.html")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(build_html(md, version))
    if "--no-pdf" in args:
        print("guide: docs/GUIDE.html")
        return
    if build_pdf(html_path, os.path.join(DOCS, "GUIDE.pdf")):
        os.remove(html_path)
        print("guide: docs/GUIDE.pdf")


if __name__ == "__main__":
    main()
