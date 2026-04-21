#!/usr/bin/env bash
# Build MiSTer inside a container using the devcontainer toolchain image.
# Forwards all args to `make`. Examples:
#   ./docker-build.sh
#   ./docker-build.sh clean
#   ./docker-build.sh DEBUG=1 V=1
#
# Env overrides:
#   MISTER_DOCKER_IMAGE=<tag>    use a different image tag
#   MISTER_DOCKER_REBUILD=1      force rebuilding the image even if it exists

set -euo pipefail

IMAGE_TAG="${MISTER_DOCKER_IMAGE:-mister-build:local}"
DOCKERFILE_DIR=".devcontainer"

cd "$(dirname "$0")"

if [[ "${MISTER_DOCKER_REBUILD:-0}" = "1" ]] || ! docker image inspect "${IMAGE_TAG}" >/dev/null 2>&1; then
    echo "Building ${IMAGE_TAG} from ${DOCKERFILE_DIR}/Dockerfile..."
    docker build --platform linux/amd64 -t "${IMAGE_TAG}" "${DOCKERFILE_DIR}"
fi

docker run --rm \
    --platform linux/amd64 \
    -v "$(pwd):/src" \
    -w /src \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    "${IMAGE_TAG}" \
    bash -c 'PATH="$(ls -d /usr/local/bin/gcc-arm-*/bin):$PATH" exec make "$@"' -- "$@"
