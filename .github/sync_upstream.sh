#!/usr/bin/env bash
# Merge upstream MiSTer-devel/Main_MiSTer into master.
# Outputs changed=true/false to $GITHUB_OUTPUT.

set -euo pipefail

UPSTREAM_REPO="https://github.com/MiSTer-devel/Main_MiSTer.git"
MAIN_BRANCH="master"

export GIT_MERGE_AUTOEDIT=no
git config --local user.name "zaparoo-ci-bot"
git config --local user.email "noreply@github.com"
git config --local rerere.enabled true

echo "Fetching upstream..."
git remote remove upstream 2>/dev/null || true
git remote add upstream "${UPSTREAM_REPO}"
git fetch --no-tags --prune --no-recurse-submodules upstream

# Train rerere from prior merge history so recurring conflicts resolve automatically.
echo ""
echo "Training rerere from merge history..."
ORIGINAL_BRANCH=$(git symbolic-ref -q HEAD 2>/dev/null) ||
ORIGINAL_HEAD=$(git rev-parse --verify HEAD 2>/dev/null) || {
    echo "ERROR: Could not determine current branch or HEAD."
    exit 1
}

mkdir -p .git/rr-cache

set +e
git rev-list --parents HEAD |
while read -r commit parent1 other_parents; do
    [ -z "${other_parents}" ] && continue
    git checkout -q "${parent1}^0" || continue
    if git merge ${other_parents} >/dev/null 2>&1; then
        continue
    fi
    if test -s .git/MERGE_RR; then
        git show -s --pretty=format:"Learning from %h %s" "${commit}"
        git rerere
        git checkout -q "${commit}" -- .
        git rerere
    fi
    git reset -q --hard
done
set -e

if [ -n "${ORIGINAL_BRANCH:-}" ]; then
    git checkout -q "${ORIGINAL_BRANCH#refs/heads/}"
elif [ -n "${ORIGINAL_HEAD:-}" ]; then
    git checkout -q "${ORIGINAL_HEAD}"
fi

echo ""
echo "Merging upstream/${MAIN_BRANCH}..."
BEFORE=$(git rev-parse HEAD)

if ! git merge -Xignore-all-space --no-edit "upstream/${MAIN_BRANCH}"; then
    echo ""
    echo "ERROR: Merge conflict. Resolve locally, commit, and push."
    echo "git rerere will remember the resolution for future runs."
    exit 1
fi

AFTER=$(git rev-parse HEAD)

if [ "${BEFORE}" = "${AFTER}" ]; then
    echo "Already up-to-date with upstream."
    echo "changed=false" >> "${GITHUB_OUTPUT:-/dev/null}"
else
    echo "Merged upstream changes ($(git rev-parse --short HEAD))."
    echo "changed=true" >> "${GITHUB_OUTPUT:-/dev/null}"
fi
