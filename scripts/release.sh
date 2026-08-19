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
# Usage: sh scripts/release.sh /path/to/repo vX.Y.Z [commit-message-file]
# Run from the root of the extracted release tree.

set -eu

REPO="${1:?usage: release.sh /path/to/repo vX.Y.Z [message-file]}"
TAG="${2:?usage: release.sh /path/to/repo vX.Y.Z [message-file]}"
MSGFILE="${3:-}"

SRC=$(pwd)
[ -f "$SRC/include/polajuice.h" ] || {
    echo "run this from the root of the release tree" >&2; exit 1; }
[ -d "$REPO/.git" ] || { echo "$REPO is not a git repository" >&2; exit 1; }

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
# Copy the release tree over the repo. Nothing in the repo outside these
# paths is touched; deletions of tracked files must be done by hand.
for entry in "$SRC"/*; do
    cp -R "$entry" "$REPO/"
done
# Keep renders and the fetched film library out of history, idempotently.
for pattern in '*.jpg' '*.JPG' '*.jpeg' '*.png' '*.ppm' 'data/luts/'; do
    grep -qxF "$pattern" .gitignore || echo "$pattern" >> .gitignore
done

echo "== building and testing from the repo state"
make clean
make
make check

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

echo
echo "== done: $(git rev-parse --short HEAD) tagged $TAG"
echo "verify with: git log --oneline --decorate --all | head -5"
