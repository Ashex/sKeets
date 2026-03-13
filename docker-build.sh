#!/usr/bin/env bash
# docker-build.sh — Cross-compile sKeets for Kobo inside Docker
#
# Usage:
#   ./docker-build.sh                                    # auto-extract update/rootfs.img if present
#   KOBO_SYSROOT=/path/to/rootfs ./docker-build.sh      # use an already-extracted sysroot
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="skeets-cross"
BUILD_DIR="${SCRIPT_DIR}/build-kobo"
HOST_UID=1000
HOST_GID=1000
NINJA_PACKAGE_TARGET="${NINJA_PACKAGE_TARGET:-kobo-package}"

# ── Detect host arch for Docker platform ──────────────────────────
# The Dockerfile handles both amd64 and arm64 natively.
# On Apple Silicon we build arm64; on Intel we build amd64.
HOST_ARCH="$(uname -m)"
case "${HOST_ARCH}" in
    x86_64)  DOCKER_PLATFORM="linux/amd64" ;;
    aarch64|arm64) DOCKER_PLATFORM="linux/arm64" ;;
    *) echo "WARNING: unknown architecture '${HOST_ARCH}', defaulting to linux/amd64" >&2
       DOCKER_PLATFORM="linux/amd64" ;;
esac
echo "==> Host architecture: ${HOST_ARCH} → Docker platform: ${DOCKER_PLATFORM}"

echo "==> Building Docker cross-compilation image …"
docker build --platform "${DOCKER_PLATFORM}" \
    -t "${IMAGE_NAME}" -f "${SCRIPT_DIR}/Dockerfile.cross" "${SCRIPT_DIR}"

mkdir -p "${BUILD_DIR}"

# ── Auto-extract sysroot from update/rootfs.img if available ──────
ROOTFS_IMG="${SCRIPT_DIR}/update/rootfs.img"
EXTRACTED_SYSROOT="${SCRIPT_DIR}/build-kobo/sysroot"

if [[ -z "${KOBO_SYSROOT:-}" && -f "${ROOTFS_IMG}" ]]; then
    if [[ ! -d "${EXTRACTED_SYSROOT}" ]]; then
        echo "==> Extracting Kobo sysroot from update/rootfs.img …"
        mkdir -p "${EXTRACTED_SYSROOT}"
        zstd -d "${ROOTFS_IMG}" -o "${BUILD_DIR}/rootfs.ext4" --force
        # Mount the ext4 image and copy contents (works without root via Docker)
        docker run --rm --privileged \
            --platform "${DOCKER_PLATFORM}" \
            -v "${BUILD_DIR}:/work" \
            "${IMAGE_NAME}" \
            bash -c "
                mkdir -p /mnt/rootfs
                mount -o loop,ro /work/rootfs.ext4 /mnt/rootfs
                cp -a /mnt/rootfs/. /work/sysroot/
                umount /mnt/rootfs
                chown -R ${HOST_UID}:${HOST_GID} /work/sysroot
            "
        rm -f "${BUILD_DIR}/rootfs.ext4"
        echo "==> Sysroot extracted to ${EXTRACTED_SYSROOT}"
    else
        echo "==> Using cached sysroot: ${EXTRACTED_SYSROOT}"
    fi
    KOBO_SYSROOT="${EXTRACTED_SYSROOT}"
fi

# ── Optional sysroot mount ────────────────────────────────────────
SYSROOT_VOLUME=()
SYSROOT_ENV=()
if [[ -n "${KOBO_SYSROOT:-}" ]]; then
    if [[ ! -d "${KOBO_SYSROOT}" ]]; then
        echo "ERROR: KOBO_SYSROOT='${KOBO_SYSROOT}' is not a directory" >&2
        exit 1
    fi
    echo "==> Using Kobo sysroot: ${KOBO_SYSROOT}"
    SYSROOT_VOLUME=(-v "${KOBO_SYSROOT}:/opt/kobo-sysroot:ro")
    SYSROOT_ENV=(-e "KOBO_SYSROOT=/opt/kobo-sysroot")
fi

echo "==> Cross-compiling sKeets for Kobo …"
echo "==> Packaging target: ${NINJA_PACKAGE_TARGET}"
docker run --rm \
    --platform "${DOCKER_PLATFORM}" \
    --user "${HOST_UID}:${HOST_GID}" \
    -v "${SCRIPT_DIR}:/src:ro" \
    -v "${BUILD_DIR}:/build" \
    "${SYSROOT_VOLUME[@]+"${SYSROOT_VOLUME[@]}"}" \
    "${SYSROOT_ENV[@]+"${SYSROOT_ENV[@]}"}" \
    -w /build \
    "${IMAGE_NAME}" \
    bash -c '
        cmake /src \
            -DCMAKE_TOOLCHAIN_FILE=/src/toolchain/arm-kobo-linux-gnueabihf.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_PREFIX_PATH=/opt/qt6-armhf \
            -DQT_HOST_PATH=/opt/qt6-host \
            -GNinja \
        && ninja -j$(nproc) '"${NINJA_PACKAGE_TARGET}"'
    '

echo ""
echo "Done!"
echo "  Binary:  ${BUILD_DIR}/bin/sKeets"
echo "  Package: ${BUILD_DIR}/KoboRoot.tgz"
