#!/usr/bin/env bash
# Build MiSTer_Zaparoo from the latest upstream stable MiSTer release, or from
# the upstream commit pinned in .github/stable_upstream.
#
# The build happens in a temporary worktree so the current checkout can stay on
# upstream master for development. The resulting binaries are copied to bin/.

set -euo pipefail

UPSTREAM_REPO="https://github.com/MiSTer-devel/Main_MiSTer.git"
UPSTREAM_REMOTE="${UPSTREAM_REMOTE:-upstream}"
UPSTREAM_BRANCH="${UPSTREAM_BRANCH:-master}"
OUTPUT_DIR="${STABLE_BUILD_OUTPUT_DIR:-bin}"
METADATA_FILE="${STABLE_BUILD_METADATA_FILE:-}"
PINNED_STABLE_COMMIT="${STABLE_BUILD_STABLE_COMMIT:-}"

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

# .github/stable_upstream pins the stable base to an upstream master commit
# instead of upstream's latest tagged release. Use it when the fork needs
# upstream changes that are not released yet, so the fork diff cannot apply to
# the release. While a pin is set, the daily sync leaves this branch alone, so
# the branch tree is exactly the pin plus the fork. Delete the file to go back
# to tagged releases once upstream releases past the pin.
PIN_FILE=".github/stable_upstream"
UPSTREAM_PIN=""
if [ -f "${PIN_FILE}" ]; then
    UPSTREAM_PIN=$(grep -v '^[[:space:]]*#' "${PIN_FILE}" | tr -d '[:space:]')
fi

if [ -n "${UPSTREAM_PIN}" ]; then
    STABLE_COMMIT=$(git rev-parse --verify --quiet "${UPSTREAM_PIN}^{commit}") || {
        echo "error: ${PIN_FILE} names ${UPSTREAM_PIN}, which is not a known commit" >&2
        exit 1
    }
    if [ -n "${PINNED_STABLE_COMMIT}" ] && [ "${PINNED_STABLE_COMMIT}" != "${STABLE_COMMIT}" ]; then
        echo "error: pinned commit changed between metadata and build" >&2
        exit 1
    fi
    if ! git merge-base --is-ancestor "${STABLE_COMMIT}" "${UPSTREAM_REF}"; then
        echo "error: ${PIN_FILE} commit ${STABLE_COMMIT} is not on ${UPSTREAM_REF}" >&2
        exit 1
    fi
    # The fork diff is taken against the pin, so the branch must contain the
    # pin and no upstream commit after it.
    BRANCH_UPSTREAM=$(git merge-base HEAD "${UPSTREAM_REF}")
    if [ "${BRANCH_UPSTREAM}" != "${STABLE_COMMIT}" ]; then
        echo "error: this branch contains upstream up to ${BRANCH_UPSTREAM}, but ${PIN_FILE} pins ${STABLE_COMMIT}" >&2
        echo "       update the pin to the upstream commit the branch merged" >&2
        exit 1
    fi
    STABLE_DATE=$(TZ=UTC git show -s --format=%cd --date=format-local:%Y%m%d "${STABLE_COMMIT}")
    STABLE_NAME="master@$(git rev-parse --short "${STABLE_COMMIT}")"
    FORK_BASE="${STABLE_COMMIT}"
else
    STABLE_COMMIT=${PINNED_STABLE_COMMIT}
    if [ -z "${STABLE_COMMIT}" ]; then
        STABLE_COMMIT=$(git log -1 --format=%H "${UPSTREAM_REF}" -- 'releases/MiSTer_*')
    fi
    if [ -z "${STABLE_COMMIT}" ]; then
        echo "error: could not find an upstream stable release commit" >&2
        exit 1
    fi
    if ! git merge-base --is-ancestor "${STABLE_COMMIT}" "${UPSTREAM_REF}"; then
        echo "error: stable commit ${STABLE_COMMIT} is not reachable from ${UPSTREAM_REF}" >&2
        exit 1
    fi

    STABLE_FILE=$(git diff-tree --no-commit-id --name-only -r "${STABLE_COMMIT}" -- 'releases/MiSTer_*' | sort | tail -n 1)
    if [ -z "${STABLE_FILE}" ]; then
        echo "error: latest upstream release commit does not add a releases/MiSTer_* file" >&2
        exit 1
    fi

    STABLE_NAME=$(basename "${STABLE_FILE}")
    STABLE_DATE=${STABLE_NAME#MiSTer_}
    FORK_BASE="${UPSTREAM_REF}"
fi

if [ -n "${METADATA_FILE}" ]; then
    cat >"${METADATA_FILE}" <<EOF
STABLE_NAME=${STABLE_NAME}
STABLE_DATE=${STABLE_DATE}
STABLE_COMMIT=${STABLE_COMMIT}
FORK_SHORT_SHA=${FORK_SHORT_SHA}
EOF
fi

if [ "${STABLE_BUILD_METADATA_ONLY:-false}" = "true" ]; then
    echo "==> Latest upstream stable release is ${STABLE_NAME}"
    exit 0
fi

TMP_WORKTREE=$(mktemp -d "${TMPDIR:-/tmp}/mister-zaparoo-stable.XXXXXX")
cleanup() {
    git worktree remove --force "${TMP_WORKTREE}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "==> Building from upstream ${STABLE_NAME} with Zaparoo ${FORK_SHORT_SHA}"
git worktree add --detach "${TMP_WORKTREE}" "${STABLE_COMMIT}" >/dev/null

# Apply the cumulative fork-only diff (relative to upstream master) so revert
# pairs and other intra-fork conflicts cancel out — replaying commit-by-commit
# would re-expose them. -3 falls back to a 3-way merge when stable's content for
# a shared file (including input.cpp / scheduler.cpp) has drifted from upstream
# master — the same mechanism used for every fork-touched upstream file, so the
# fork diff stays the single source of truth. MiSTer.ini is excluded: the fork's
# only change is an uncomment of a default-valued line, and stable's example ini
# drifts often enough to cause spurious conflicts.
FORK_DIFF=$(git diff --binary "${FORK_BASE}..${FORK_HEAD}" -- . \
    ':(exclude)MiSTer.ini')
if [ -n "${FORK_DIFF}" ]; then
    printf '%s\n' "${FORK_DIFF}" | git -C "${TMP_WORKTREE}" apply -3 --index
fi

"${TMP_WORKTREE}/docker-build.sh" "$@"

mkdir -p "${OUTPUT_DIR}"
cp "${TMP_WORKTREE}/bin/MiSTer_Zaparoo" "${OUTPUT_DIR}/MiSTer_Zaparoo"
cp "${TMP_WORKTREE}/bin/MiSTer_Zaparoo.elf" "${OUTPUT_DIR}/MiSTer_Zaparoo.elf"

echo "==> Built ${OUTPUT_DIR}/MiSTer_Zaparoo from ${STABLE_NAME}"
