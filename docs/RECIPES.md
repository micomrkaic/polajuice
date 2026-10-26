# Recipes

A recipe is the whole look as text: one `key=value` per line, `#`
comments, blank lines ignored. The engine writes it, embeds it in every
JPEG it saves, and the page shows the one it read back from the last
render. `key=value` sources into a shell, which is why it is not JSON.

    # polajuice recipe 1
    camera=super8
    film=kodak_kodachrome_64
    print=none
    filter=orange
    strength=1
    develop=normal
    age=0.25
    grain=silver
    edge=1
    ev=-0.5
    halation=1.5
    seed=7
    crop=camera
    temp=0
    tint=0
    black=0
    shadows=0
    highlights=0
    contrast=0
    grad=0
    gradangle=0
    gradpos=0
    denoise=0
    distortion=0
    devignette=0
    rotate=0
    trimx=0
    trimy=0
    trimw=0
    trimh=0

| key | values |
|---|---|
| camera | a camera name from `polajuice cameras` |
| film | a library stem, `none` (scalar engine) or `auto` (the camera's canonical stock) |
| print | a print-stock stem or `none` |
| filter | `none`, `yellow`, `orange`, `red`, `green`, `blue` |
| strength | 0..1 |
| develop | `normal`, `push+N`, `pull-N` (N stops, -3..+4, fractions allowed: `push+1.5`), `cross`, `cross+N` |
| age | 0..1 |
| grain | `classic`, `silver` |
| edge | 0..2 |
| ev | -4..4 |
| halation | 0..2 (0 off, 1 the camera's own) |
| seed | integer |
| crop | `camera` (the camera's own format cut from the middle) or `full` (keep the whole input frame; an instant camera borders the full frame) |
| temp | white balance, warm(+)/cool(-) in stops of red-to-blue ratio, -2..2 |
| tint | magenta(+)/green(-), -1..1 |
| black | black point in scene units: crush(+)/fog(-), -0.1..0.1 |
| shadows | lift(+)/drop(-) the shadows, stops at four stops under gray, -2..2 |
| highlights | the raw shoulder's knee: softer(-)/harder(+), -1..1; scene-linear input only |
| contrast | scene contrast about middle gray, -1..1 |
| grad | graduated ND density at the dark edge, stops, 0..3 |
| gradangle | the dark edge: 0 top, 90 right, 180 bottom, 270 left |
| gradpos | the transition's center, offset from the frame center, -0.5..0.5 |
| denoise | chroma denoise, 0..3 (about 2 px per unit at full size), luminance untouched |
| distortion | radial lens correction, -0.3..0.3: + undoes barrel, - pincushion |
| devignette | undo the lens's corner falloff, stops at the corners, 0..2 |
| rotate | straighten, degrees, -15..15, + clockwise; corners outside come in black |
| trimx, trimy, trimw, trimh | a crop of the frame in fractions, before the camera's format crop; width or height 0 = none |

A JPEG's comment carries one more line after the recipe,
`# geometry in=W,H crop=X,Y,W,H place=X,Y out=W,H orig=W,H trim=X,Y
rot=D`: the frame the camera saw (after straightening and trim), the
crop it took from it and where that sits in the output (inside an
instant print's border, say), the file's own frame, where the trim
sits in it and the straightening applied. It is a comment, so recipe
readers skip it; the page uses it to line comparisons up.

Every key is optional: a partial recipe sets what it names and leaves
the rest to the defaults, or to flags.

## Replaying

    polajuice apply IN.jpg --recipe look.recipe
    polajuice apply IN.jpg --recipe render.jpg        # any JPEG polajuice saved
    polajuice apply IN.jpg --recipe look.recipe --ev 0   # flags after it override
    polajuice apply IN.jpg -c super8 ... --save-recipe look.recipe
    polajuice inspect render.jpg                        # prints the embedded recipe
    superjuice clip.mp4 --recipe look.recipe
    RECIPE=look.recipe sh scripts/super8ify.sh clip.mov  # only knobs you set go on top

The recipe rides in the JPEG as a COM segment right after the JFIF
header, so it survives any tool that preserves comments and is lost by
any that re-encodes. PNG and PPM output carry no recipe (yet).

## The page

The workspace page writes nothing it did not get from the engine: the
recipe box shows the comment read back from the last render's JPEG.
"save .recipe" downloads it; "load…" takes a .recipe file or a
polajuice JPEG; "keep" stores it under a name in the browser's local
storage; the equivalent `polajuice apply` command line is shown
alongside for terminal work and batch runs.
