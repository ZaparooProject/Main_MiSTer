#!/usr/bin/env bash
# Build MiSTer_Zaparoo from upstream master HEAD and publish a rolling unstable
# release. Companion to build_release.sh (which targets upstream stable).
# Requires GH_TOKEN in environment.

set -euo pipefail

RELEASE_TAG="${UNSTABLE_RELEASE_TAG:-MiSTer_Zaparoo_unstable}"

METADATA_FILE=$(mktemp)
UNSTABLE_BUILD_METADATA_FILE="${METADATA_FILE}" UNSTABLE_BUILD_METADATA_ONLY=true ./unstable-build.sh

# shellcheck disable=SC1090
source "${METADATA_FILE}"

if [ "${SKIP_EXISTING_RELEASE:-false}" = "true" ] && \
   gh release view "${RELEASE_TAG}" 2>/dev/null | grep -q "${UNSTABLE_NAME}_${FORK_SHORT_SHA}"; then
    echo "Unstable release for ${UNSTABLE_NAME}+${FORK_SHORT_SHA} already published; skipping."
    exit 0
fi

UNSTABLE_BUILD_METADATA_FILE="${METADATA_FILE}" \
    UNSTABLE_BUILD_UNSTABLE_COMMIT="${UNSTABLE_COMMIT}" \
    ./unstable-build.sh

# shellcheck disable=SC1090
source "${METADATA_FILE}"

ARTIFACT_BIN="bin/${UNSTABLE_NAME}_${FORK_SHORT_SHA}"
ARTIFACT_ELF="${ARTIFACT_BIN}.elf"
cp "bin/MiSTer_Zaparoo" "${ARTIFACT_BIN}"
cp "bin/MiSTer_Zaparoo.elf" "${ARTIFACT_ELF}"

# Rolling tag: replace the release on every successful build.
gh release delete "${RELEASE_TAG}" --cleanup-tag --yes 2>/dev/null || true

gh release create "${RELEASE_TAG}" \
    --prerelease \
    --title "Zaparoo unstable @ ${UNSTABLE_NAME}" \
    --notes "Automated build from upstream master HEAD (${UNSTABLE_NAME}) with Zaparoo commit ${FORK_SHORT_SHA}.

Tracks the upstream MiSTer master branch tip — equivalent to the upstream MiSTer-unstable-nightlies build, but with the Zaparoo fork applied. Updated on every push and daily upstream sync." \
    "${ARTIFACT_BIN}" \
    "${ARTIFACT_ELF}"
