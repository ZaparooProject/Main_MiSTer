#!/usr/bin/env bash
# Build MiSTer_Zaparoo from upstream master HEAD (the "unstable" tip).
#
# Mirrors stable-build.sh but skips the releases/MiSTer_* lookup. Builds in a
# temporary worktree at upstream HEAD with the Zaparoo commits cherry-picked on
# top, so the current checkout stays untouched.

set -euo pipefail

UPSTREAM_REPO="https://github.com/MiSTer-devel/Main_MiSTer.git"
UPSTREAM_REMOTE="${UPSTREAM_REMOTE:-upstream}"
UPSTREAM_BRANCH="${UPSTREAM_BRANCH:-master}"
OUTPUT_DIR="${UNSTABLE_BUILD_OUTPUT_DIR:-bin}"
METADATA_FILE="${UNSTABLE_BUILD_METADATA_FILE:-}"
PINNED_UNSTABLE_COMMIT="${UNSTABLE_BUILD_UNSTABLE_COMMIT:-}"

cd "$(dirname "$0")"

FORK_HEAD=$(git rev-parse HEAD)
FORK_SHORT_SHA=$(git rev-parse --short HEAD)

if ! git remote get-url "${UPSTREAM_REMOTE}" >/dev/null 2>&1; then
    git remote add "${UPSTREAM_REMOTE}" "${UPSTREAM_REPO}"
elif [ "${UPSTREAM_REMOTE}" = "upstream" ]; then
    UPSTREAM_URL=$(git remote get-url "${UPSTREAM_REMOTE}")
    if [ "${UPSTREAM_URL}" != "${UPSTREAM_REPO}" ] && [ "${UPSTREAM_URL}" != "git@github.com:MiSTer-devel/Main_MiSTer.git" ] && [ "${UPSTREAM_URL}" != "ssh://git@github.com/MiSTer-devel/Main_MiSTer.git" ]; then
        echo "error: upstream remote points to ${UPSTREAM_URL}, expected ${UPSTREAM_REPO}" >&2
        echo "       set UPSTREAM_REMOTE to use a custom remote intentionally" >&2
        exit 1
    fi
fi

git fetch --no-tags --prune --no-recurse-submodules "${UPSTREAM_REMOTE}" \
    "+refs/heads/${UPSTREAM_BRANCH}:refs/remotes/${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}"

UPSTREAM_REF="${UPSTREAM_REMOTE}/${UPSTREAM_BRANCH}"
UNSTABLE_COMMIT=${PINNED_UNSTABLE_COMMIT}
if [ -z "${UNSTABLE_COMMIT}" ]; then
    UNSTABLE_COMMIT=$(git rev-parse "${UPSTREAM_REF}")
fi
if [ -z "${UNSTABLE_COMMIT}" ]; then
    echo "error: could not resolve upstream HEAD" >&2
    exit 1
fi
if ! git merge-base --is-ancestor "${UNSTABLE_COMMIT}" "${UPSTREAM_REF}"; then
    echo "error: unstable commit ${UNSTABLE_COMMIT} is not reachable from ${UPSTREAM_REF}" >&2
    exit 1
fi

UNSTABLE_SHORT_SHA=$(git rev-parse --short=8 "${UNSTABLE_COMMIT}")
UNSTABLE_DATE=$(TZ=UTC git log -1 --format=%cd --date=format-local:%Y%m%d "${UNSTABLE_COMMIT}")
UNSTABLE_NAME="MiSTer_unstable_${UNSTABLE_DATE}_${UNSTABLE_SHORT_SHA}"

if [ -n "${METADATA_FILE}" ]; then
    cat >"${METADATA_FILE}" <<EOF
UNSTABLE_NAME=${UNSTABLE_NAME}
UNSTABLE_DATE=${UNSTABLE_DATE}
UNSTABLE_COMMIT=${UNSTABLE_COMMIT}
UNSTABLE_SHORT_SHA=${UNSTABLE_SHORT_SHA}
FORK_SHORT_SHA=${FORK_SHORT_SHA}
EOF
fi

if [ "${UNSTABLE_BUILD_METADATA_ONLY:-false}" = "true" ]; then
    echo "==> Upstream master HEAD is ${UNSTABLE_NAME}"
    exit 0
fi

TMP_WORKTREE=$(mktemp -d "${TMPDIR:-/tmp}/mister-zaparoo-unstable.XXXXXX")
cleanup() {
    git worktree remove --force "${TMP_WORKTREE}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "==> Building from upstream ${UNSTABLE_NAME} with Zaparoo ${FORK_SHORT_SHA}"
git worktree add --detach "${TMP_WORKTREE}" "${UNSTABLE_COMMIT}" >/dev/null
git -C "${TMP_WORKTREE}" merge --no-edit -Xignore-all-space "${FORK_HEAD}"

"${TMP_WORKTREE}/docker-build.sh" "$@"

mkdir -p "${OUTPUT_DIR}"
cp "${TMP_WORKTREE}/bin/MiSTer_Zaparoo" "${OUTPUT_DIR}/MiSTer_Zaparoo"
cp "${TMP_WORKTREE}/bin/MiSTer_Zaparoo.elf" "${OUTPUT_DIR}/MiSTer_Zaparoo.elf"

echo "==> Built ${OUTPUT_DIR}/MiSTer_Zaparoo from ${UNSTABLE_NAME}"
