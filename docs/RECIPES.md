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

A JPEG's comment carries one more line after the recipe,
`# geometry in=W,H crop=X,Y,W,H place=X,Y out=W,H`: the crop the camera
took from the input and where it sits in the output (inside an instant
print's border, say). It is a comment, so recipe readers skip it; the
page uses it to line comparisons up.

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
