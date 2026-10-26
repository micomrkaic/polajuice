#!/bin/sh
# release.sh - import this source tree into the polajuice git repo, verify,
# commit, tag and push. Encodes the failure modes we hit by hand:
#   - refuses to run if the repo has uncommitted changes (no surprises)
#   - refuses if local main is not exactly at origin/main (no non-fast-forward)
#   - refuses if the tag already exists locally or remotely (no tag surgery)
#   - guarantees .gitignore keeps renders and the film library out
#   - builds and runs the test suite from the repo state before committing
#   - never uses rm -rf, never force-pushes
#
# Usage: sh scripts/release.sh [/path/to/repo] [vX.Y.Z] [message-file]
# Run from the root of the extracted release tree (or invoke by path -
# the script locates its own tree). Every argument is derivable and
# optional: the repo defaults to the sibling directory named polajuice,
# the tag to v$(cat VERSION), the message file to COMMIT_MSG_$VERSION.
# The zero-argument ritual:
#     cd ~/work/c_progs/polajuice
#     tar xzf ~/Downloads/polajuice-X.Y.Z.tar.gz
#     sh polajuice-src/scripts/release.sh

set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$SRC/include/polajuice.h" ] || {
    echo "cannot locate the release tree from $0" >&2; exit 1; }
cd "$SRC"

TREE_VERSION=$(cat "$SRC/VERSION")

# Every machine releases complete: the film library is not in git (30 MB
# of other people's LUTs), so fetch it when it is missing. fetch_luts.sh
# skips families already present, so this is a no-op on a machine that
# has them. A machine without network, or with a failed fetch, cannot
# publish a page without films by accident: it stops here.
ensure_films() {
    if [ -d data/luts ] && \
       [ -n "$(find data/luts -name '*.cube' -print -quit 2>/dev/null)" ]; then
        return 0
    fi
    echo "== no film library on this machine; fetching (make fetch-luts)"
    sh scripts/fetch_luts.sh || {
        echo "!! film library fetch failed; not deploying a page without films" >&2
        echo "   (fix the network or run 'make fetch-luts', then" >&2
        echo "    'sh scripts/release.sh --restage' to publish the page)" >&2
        return 1
    }
}

# Publish $REPO/web/ as the gh-pages branch from an orphan staging
# branch. The staging tree is emptied with git rm first, which removes
# .gitignore from it, so the *.jpg rule of main cannot swallow the
# sample photos (the restage path once branched from main and pushed a
# strip with captions and no pictures). Runs in $REPO.
deploy_pages() {
    stamp=$(git rev-parse --short HEAD)
    git branch -D gh-pages-staging >/dev/null 2>&1 || true
    git worktree prune
    work=$(mktemp -d)
    git worktree add --detach "$work" >/dev/null
    (
        cd "$work"
        git checkout -q --orphan gh-pages-staging
        git rm -rfq . 2>/dev/null || true
        cp -R "$REPO/web/." .
        git add -A
        n=$(git ls-files samples | grep -c '\.jpg$' || true)
        [ "$n" -ge 12 ] || {
            echo "!! only $n sample photos staged for gh-pages (expected 12);" >&2
            echo "   refusing to publish a strip without pictures" >&2
            exit 1
        }
        git commit -qm "pages build from $stamp"
        git push --force origin HEAD:gh-pages
    ) || { git worktree remove --force "$work"; exit 1; }
    git worktree remove --force "$work"
    git branch -D gh-pages-staging >/dev/null 2>&1 || true
}

# --restage: redeploy gh-pages from the CURRENT repo state (fresh film
# staging included) without importing, committing, tagging or touching
# main. For when the film library changed but the source did not -
# e.g. after fetch_luts.sh --rawtherapee converts a new collection.
if [ "${1:-}" = "--restage" ]; then
    REPO="${2:-$(dirname "$SRC")/polajuice}"
    [ -d "$REPO/.git" ] || { echo "$REPO is not a git repository" >&2; exit 1; }
    cd "$REPO"
    echo "== restaging films and redeploying gh-pages (no release)"
    ensure_films || exit 1
    sh scripts/stage_web_films.sh
    deploy_pages
    echo "== done: gh-pages restaged from current repo (main untouched)"
    exit 0
fi

REPO="${1:-$(dirname "$SRC")/polajuice}"
TAG="${2:-v$TREE_VERSION}"
MSGFILE="${3:-COMMIT_MSG_$TREE_VERSION}"
[ -f "$SRC/$MSGFILE" ] || MSGFILE=""
echo "== releasing $TAG to $REPO${MSGFILE:+ (message: $MSGFILE)}"
[ -d "$REPO/.git" ] || { echo "$REPO is not a git repository" >&2; exit 1; }

# tripwires for the double-extraction failure class: a nested staging
# tree in the source means the tarball was untarred from inside
# polajuice-src (it would be imported and committed wholesale); one
# already inside the repo means a past mis-extraction needs cleaning.
[ -d "$SRC/polajuice-src" ] && {
    echo "source tree contains a nested polajuice-src/ - the tarball was" >&2
    echo "extracted from the wrong directory. Untar from the parent" >&2
    echo "directory (the one CONTAINING polajuice-src), then retry." >&2
    exit 1; }
[ -d "$REPO/polajuice-src" ] && {
    echo "the repo contains a stray polajuice-src/ tree - remove it" >&2
    echo "(and commit the removal if tracked) before releasing." >&2
    exit 1; }

PAGE_V=$(grep -o 'PAGE_VERSION = "[^"]*"' "$SRC/web/index.html" | cut -d'"' -f2)
TREE_V=$(cat "$SRC/VERSION")
[ "$PAGE_V" = "$TREE_V" ] || {
    echo "page version ($PAGE_V) != tree version ($TREE_V); fix index.html" >&2
    exit 1; }

[ -f "$SRC/web/polajuice.wasm" ] || {
    echo "no web/polajuice.wasm in this tree; refuse to release without the engine" >&2
    echo "(release tarballs ship it prebuilt; scripts/build_web.sh rebuilds it)" >&2
    exit 1; }

echo "== preflight"
cd "$REPO"
git remote get-url origin >/dev/null || {
    echo "repo has no 'origin' remote" >&2; exit 1; }
if [ -n "$(git status --porcelain)" ]; then
    echo "repo has uncommitted changes; commit or stash them first" >&2
    git status --short >&2
    exit 1
fi
git fetch origin
LOCAL=$(git rev-parse main)
REMOTE=$(git rev-parse origin/main)
if [ "$LOCAL" != "$REMOTE" ]; then
    echo "local main ($LOCAL) != origin/main ($REMOTE);" >&2
    echo "reconcile first so the push is a plain fast-forward" >&2
    exit 1
fi
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    echo "tag $TAG already exists locally; pick the next version" >&2; exit 1
fi
if git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1; then
    echo "tag $TAG already exists on origin; pick the next version" >&2; exit 1
fi

# before the import: a machine that cannot fetch the library cannot push
# either, and stopping here leaves the repo exactly as it was
ensure_films || exit 1

echo "== importing source tree"
# Copy the release tree over the repo, dotfiles included (a plain * glob
# silently skips .gitignore, which once let a build artifact reach main).
# Nothing in the repo outside these paths is touched; deletions of
# tracked files must be done by hand.
for entry in "$SRC"/* "$SRC"/.[!.]*; do
    [ -e "$entry" ] || continue
    case "$(basename "$entry")" in .git) continue ;; esac
    cp -R "$entry" "$REPO/"
done
# Keep renders and the fetched film library out of history, idempotently.
for pattern in '*.jpg' '*.JPG' '*.jpeg' '*.png' '*.ppm' 'data/luts/' \
               'web/polajuice.wasm' 'web/films/' 'third_party/wasi-sdk*' \
               'web/_pagecheck.mjs' '*.d' '/superjuice' '/tests/test_web_shim' \
               '!web/samples/*.jpg' '!web/samples/*.json' \
               '*.DNG' '*.dng' '*.NEF' '*.nef' '*.xmp' '/*.cube' \
               'node_modules' 'scripts/package.json' 'scripts/package-lock.json' \
               '!tests/raw/*'; do
    grep -qxF "$pattern" .gitignore || echo "$pattern" >> .gitignore
done
# the last match wins in .gitignore: the un-ignore of the synthetic raws
# must follow the raw ignores, wherever an older checkout had it
grep -vxF '!tests/raw/*' .gitignore > .gitignore.tmp
echo '!tests/raw/*' >> .gitignore.tmp
mv .gitignore.tmp .gitignore
# the sample photos used to be ignored and lived only in working trees
# and on gh-pages, so a fresh clone deployed a strip without pictures;
# they are tracked from 1.16.1 on - drop the old rule and add them
if grep -qxF 'web/samples/' .gitignore; then
    grep -vxF 'web/samples/' .gitignore > .gitignore.tmp
    mv .gitignore.tmp .gitignore
fi
git add -f web/samples tests/raw
# build byproducts that slipped into history at some point (the .d
# dependency files, the superjuice binary) dirty the tree on every make
# and block the clean-tree check on the next release; drop them from the
# index once, idempotently
git rm -q --cached --ignore-unmatch -- '*.d' superjuice tests/test_web_shim \
    >/dev/null 2>&1 || true

echo "== building and testing from the repo state"
make clean
make
make check
if command -v node >/dev/null; then
    echo "== verifying the shipped wasm engine (node)"
    ( cd scripts && node test_wasm.mjs && node test_page.mjs && node test_workspace.mjs )
else
    echo "note: node not found; shipping wasm without local verification" >&2
fi

echo "== committing"
git add -A
if [ -z "$(git status --porcelain)" ]; then
    echo "nothing to commit: repo already matches this tree" >&2; exit 1
fi
git status --short
if [ -n "$MSGFILE" ] && [ -f "$SRC/$MSGFILE" ]; then
    git commit -F "$SRC/$MSGFILE"
else
    git commit -m "Release $TAG"
fi
git tag -a "$TAG" -m "Release $TAG"

echo "== pushing (fast-forward + new tag, no force)"
git push origin main "refs/tags/$TAG"

echo "== deploying web/ to gh-pages"
sh scripts/stage_web_films.sh
STAMP=$(git rev-parse --short HEAD)
deploy_pages

echo
echo "== done: $STAMP tagged $TAG, gh-pages updated"
echo "first release only: repo Settings -> Pages -> branch gh-pages, folder /"
echo "verify with: git log --oneline --decorate --all | head -5"
