#!/usr/bin/env bash
# Build MiSTer binary and publish a GitHub Release.
# Requires GH_TOKEN in environment.

set -euo pipefail

source setup_default_toolchain.sh
make

RELEASE_NAME="MiSTer_$(date -u +%Y%m%d)"
cp bin/MiSTer "${RELEASE_NAME}"
cp bin/MiSTer.elf "${RELEASE_NAME}.elf"

# Last build of the day wins.
gh release delete "${RELEASE_NAME}" --cleanup-tag --yes 2>/dev/null || true

SHORT_SHA=$(git rev-parse --short HEAD)
gh release create "${RELEASE_NAME}" \
    --title "${RELEASE_NAME}" \
    --notes "Automated build from commit ${SHORT_SHA}" \
    "${RELEASE_NAME}" \
    "${RELEASE_NAME}.elf"
