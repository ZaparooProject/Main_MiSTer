#!/usr/bin/env bash
# Copyright (c) 2020 José Manuel Barroso Galindo <theypsilon@gmail.com>
# Adapted from https://github.com/MiSTer-DB9/Main_MiSTer
#
# Build MiSTer binary from the latest upstream stable release and publish it.
# Requires GH_TOKEN in environment.

set -euo pipefail

METADATA_FILE=$(mktemp)
STABLE_BUILD_METADATA_FILE="${METADATA_FILE}" STABLE_BUILD_METADATA_ONLY=true ./stable-build.sh

# shellcheck disable=SC1090
source "${METADATA_FILE}"
RELEASE_TAG="MiSTer_Zaparoo_${STABLE_DATE}"
RELEASE_REPO="${RELEASE_REPO:-${GITHUB_REPOSITORY:-ZaparooProject/Main_MiSTer}}"

if [ "${SKIP_EXISTING_RELEASE:-false}" = "true" ] && gh release view "${RELEASE_TAG}" -R "${RELEASE_REPO}" >/dev/null 2>&1; then
    echo "Release ${RELEASE_TAG} already exists; skipping."
    exit 0
fi

STABLE_BUILD_METADATA_FILE="${METADATA_FILE}" STABLE_BUILD_STABLE_COMMIT="${STABLE_COMMIT}" ./stable-build.sh

# shellcheck disable=SC1090
source "${METADATA_FILE}"

# Last build of the day wins.
gh release delete "${RELEASE_TAG}" -R "${RELEASE_REPO}" --cleanup-tag --yes 2>/dev/null || true

gh release create "${RELEASE_TAG}" \
    -R "${RELEASE_REPO}" \
    --title "${RELEASE_TAG}" \
    --notes "Automated build from upstream ${STABLE_NAME} with Zaparoo commit ${FORK_SHORT_SHA}" \
    "bin/MiSTer_Zaparoo" \
    "bin/MiSTer_Zaparoo.elf"
