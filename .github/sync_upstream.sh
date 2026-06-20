#!/usr/bin/env bash
# Copyright (c) 2020 José Manuel Barroso Galindo <theypsilon@gmail.com>
# Adapted from https://github.com/MiSTer-DB9/Main_MiSTer
#
# Merge upstream MiSTer-devel/Main_MiSTer into both release-channel branches:
#   master  -> integration/beta branch, feeds the unstable prerelease channel
#   stable  -> promoted branch, feeds the distribution stable channel
#
# Both branches must keep absorbing upstream merges so that each channel's fork
# diff (upstream/master..<branch>) stays fork-only. Emits <branch>_changed=true/false
# to $GITHUB_OUTPUT so the workflow can push/build only what moved.

set -euo pipefail

UPSTREAM_REPO="https://github.com/MiSTer-devel/Main_MiSTer.git"
MAIN_BRANCH="master"
# Channel branches to keep in sync with upstream/master.
SYNC_BRANCHES="${SYNC_BRANCHES:-master stable}"

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

# Merge upstream into each channel branch. rerere (trained above, shared cache)
# auto-resolves recurring conflicts; stable's conflicts are a subset of master's.
sync_branch() {
    local branch="$1"
    echo ""
    echo "=== Syncing ${branch} <- upstream/${MAIN_BRANCH} ==="

    if ! git rev-parse --verify --quiet "origin/${branch}" >/dev/null; then
        echo "origin/${branch} does not exist yet; skipping."
        echo "${branch}_changed=false" >> "${GITHUB_OUTPUT:-/dev/null}"
        return 0
    fi

    git checkout -q -B "${branch}" "origin/${branch}"
    local before after
    before=$(git rev-parse HEAD)

    if ! git merge -Xignore-all-space --no-edit "upstream/${MAIN_BRANCH}"; then
        echo ""
        echo "ERROR: Merge conflict on ${branch}. Resolve locally, commit, and push."
        echo "git rerere will remember the resolution for future runs."
        exit 1
    fi

    after=$(git rev-parse HEAD)
    if [ "${before}" = "${after}" ]; then
        echo "${branch} already up-to-date with upstream."
        echo "${branch}_changed=false" >> "${GITHUB_OUTPUT:-/dev/null}"
    else
        echo "Merged upstream into ${branch} ($(git rev-parse --short HEAD))."
        echo "${branch}_changed=true" >> "${GITHUB_OUTPUT:-/dev/null}"
    fi
}

for branch in ${SYNC_BRANCHES}; do
    sync_branch "${branch}"
done

# Leave the checkout on the main branch for subsequent steps.
git checkout -q "${MAIN_BRANCH}"
