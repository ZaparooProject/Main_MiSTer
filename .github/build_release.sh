#!/usr/bin/env bash
# Copyright (c) 2020 José Manuel Barroso Galindo <theypsilon@gmail.com>
# Adapted from https://github.com/MiSTer-DB9/Main_MiSTer
#
# Build MiSTer binary and publish a GitHub Release.
# Requires GH_TOKEN in environment.

set -euo pipefail

# Use the toolchain script's defaults; ignore any inherited runner env.
MISTER_GCC_INSTALL_DIR=""
MISTER_GCC_VER=""
MISTER_GCC_HOST_ARCH=""
source setup_default_toolchain.sh
make PRJ=MiSTer_Zaparoo

RELEASE_TAG="MiSTer_Zaparoo_$(date -u +%Y%m%d)"

# Last build of the day wins.
gh release delete "${RELEASE_TAG}" --cleanup-tag --yes 2>/dev/null || true

SHORT_SHA=$(git rev-parse --short HEAD)
gh release create "${RELEASE_TAG}" \
    --title "${RELEASE_TAG}" \
    --notes "Automated build from commit ${SHORT_SHA}" \
    "bin/MiSTer_Zaparoo" \
    "bin/MiSTer_Zaparoo.elf"
