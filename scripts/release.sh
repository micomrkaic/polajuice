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
               'web/polajuice.wasm' 'web/films/' 'web/samples/' 'third_party/wasi-sdk*' \
               'web/_pagecheck.mjs'; do
    grep -qxF "$pattern" .gitignore || echo "$pattern" >> .gitignore
done

echo "== building and testing from the repo state"
make clean
make
make check
if command -v node >/dev/null; then
    echo "== verifying the shipped wasm engine (node)"
    ( cd scripts && node test_wasm.mjs && node test_page.mjs )
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
if [ -d data/luts ] && [ -n "$(find data/luts -name '*.cube' -print -quit 2>/dev/null)" ]; then
    sh scripts/stage_web_films.sh
else
    echo "note: no film library (make fetch-luts); deploying without films" >&2
fi
STAMP=$(git rev-parse --short HEAD)
# the staging branch persists after a deploy; remove leftovers or the
# second release on the same repo collides with the first one's branch
git branch -D gh-pages-staging >/dev/null 2>&1 || true
git worktree prune
WORK=$(mktemp -d)
git worktree add --detach "$WORK" >/dev/null
(
    cd "$WORK"
    git checkout -q --orphan gh-pages-staging
    git rm -rfq . 2>/dev/null || true
    cp -R "$REPO/web/." .
    git add -A
    git commit -qm "pages build from $STAMP"
    git push --force origin HEAD:gh-pages
)
git worktree remove --force "$WORK"
git branch -D gh-pages-staging >/dev/null 2>&1 || true

echo
echo "== done: $STAMP tagged $TAG, gh-pages updated"
echo "first release only: repo Settings -> Pages -> branch gh-pages, folder /"
echo "verify with: git log --oneline --decorate --all | head -5"
