# sKeets Cross-Compilation Build Status

**Date**: 2026-03-10
**Project**: sKeets — Bluesky/AT Protocol client for Kobo e-readers
**Target**: Kobo Libra Colour / Clara Colour (ARM Cortex-A35, armhf 32-bit)

---

## Current State

**BUILD SUCCESSFUL** ✅

- Binary: `build-kobo/sKeets` (122K, ELF 32-bit ARM, dynamically linked)
- Package: `build-kobo/sKeets-koboroot.tgz` (82K)
- All 97 compilation steps completed with zero errors
- Docker image `skeets-cross` cached for fast incremental rebuilds

---

## What Changed (All Files Modified This Session)

### 1. `Dockerfile.cross` — Complete rewrite
- **3-stage build**: `qt-host-builder` → `qt-target-builder` → final image
- Stage separation ensures host Qt build cache survives armhf package changes
- Qt 6.5.2 from source: https://download.qt.io/archive/qt/6.5/6.5.2/single/
- Host build: qtbase + qtshadertools + qtdeclarative (provides moc, rcc, uic, qsb, qmltyperegistrar)
- Target cross-build flags: `-no-opengl -no-dbus -no-icu -no-fontconfig -qt-freetype -qt-pcre -system-zlib -qt-doubleconversion -openssl-runtime`
- Cross-toolchain includes `-D_FILE_OFFSET_BITS=64` for Ubuntu 24.04 armhf glibc Y2038 compat
- Installed to `/opt/qt6-host` (native) and `/opt/qt6-armhf` (target)

### 2. `docker-build.sh` — Updated paths & removed workarounds
- Removed `SYSROOT_CMAKE_ARGS` (OpenGL workarounds no longer needed)
- Removed deletion of Qt6 libs from sysroot extraction
- cmake invocation now uses `-DCMAKE_PREFIX_PATH=/opt/qt6-armhf -DQT_HOST_PATH=/opt/qt6-host`
- Switched to single-quoted bash block (no variable interpolation needed)

### 3. `toolchain/arm-kobo-linux-gnueabihf.cmake` — Dropped CMAKE_SYSROOT
- **Key change**: Uses `CMAKE_FIND_ROOT_PATH` instead of `CMAKE_SYSROOT`
- This prevents GCC `--sysroot` from redirecting all library search paths
- Search policies set to BOTH for libraries/includes/packages
- Retains Cortex-A35 CPU flags: `-mcpu=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard -mthumb`

### 4. `cmake/patch-atproto.cmake` — Updated for Qt 6.5 + cross-compilation
- **Section 0 (NEW)**: Patches top-level SDK CMakeLists.txt to disable ASAN (`add_compile_options(-fsanitize=address)` + `add_link_options(-fsanitize=address)`)
- **Section 1**: Patches lib/CMakeLists.txt — VERSION in qt_add_qml_module, -Werror removal, ASAN/coverage block disabled
- **Section 1d (NEW)**: Removes `-lgcov` link dependency
- **Section 2**: QString::slice() → chop(1) (slice is Qt 6.8+)
- **Section 3**: moveToThread() void return fix (bool only in Qt 6.7+)
- **Section 4**: Xrpc::Client constructor fix
- **REMOVED**: matchView() → match() patch (Qt 6.5 already has matchView)

### 5. `CMakeLists.txt` — Minor version bump
- `find_package(Qt6 6.5 REQUIRED ...)` (was 6.4)

### 6. `.dockerignore` — Created
- Excludes `build-kobo/`, `update/`, `.git/` from Docker context

---

## Build History & Issues Resolved

### Build 1 — Failed: Invalid `-ssl -openssl`
- Qt 6.5.2 configure doesn't accept `-ssl -openssl`, needs `-openssl-runtime`

### Build 2 — Failed: `_TIME_BITS=64` error
- Ubuntu 24.04 armhf glibc requires `_FILE_OFFSET_BITS=64` when `_TIME_BITS=64` is set
- Fixed by adding `-D_FILE_OFFSET_BITS=64` to the Qt cross-toolchain and sKeets toolchain

### Build 3 — Failed: Qt bundled zlib breaks `_FILE_OFFSET_BITS`
- Qt's bundled zlib `gzguts.h` `#undef`s `_FILE_OFFSET_BITS` when `_LARGEFILE64_SOURCE` is defined
- Fixed by switching from `-qt-zlib` to `-system-zlib` and adding `zlib1g-dev:armhf`

### Build 4 — Failed: `qsb` missing `libX11.so.6` in cross-build stage
- Host Qt was built with OpenGL/X11 support, so `qsb` tool links against `libX11`
- The `qt-target-builder` stage lacked native X11 runtime libs
- Fixed by adding `libgl-dev libx11-6` to `qt-target-builder` apt packages

### Build 5 — Failed: Missing `libz.so` (armhf) at sKeets link time
- Qt was built with `-system-zlib`, so `libQt6Core.so` and `libQt6Network.so` link dynamically to zlib
- The final Docker stage only had `libssl-dev:armhf`, not `zlib1g-dev:armhf`
- Fixed by adding `zlib1g-dev:armhf` to the final stage

### Build 6 — Failed: armhf apt packages 404 Not Found
- `archive.ubuntu.com` does not carry armhf packages — they live on `ports.ubuntu.com`
- Fixed by adding a separate apt sources file for armhf pointing to `ports.ubuntu.com/ubuntu-ports`
- Applied to both `qt-target-builder` and final stages

### Build 7 (Build 8 internally) — SUCCESS ✅
- All previous fixes applied
- Qt 6.5.2 host build: 4930/4930 steps, zero errors
- Qt 6.5.2 armhf cross-build: 4485/4485 steps, zero errors
- sKeets build: 97/97 steps, zero errors
- KoboRoot.tgz packaged successfully

---

## Next Steps

1. **Deploy to Kobo**: Copy `sKeets-koboroot.tgz` to Kobo's root and restart, or manually extract to `/mnt/onboard/.adds/sKeets/`
2. **Test on device**: Verify the binary runs and can connect to Bluesky
3. **Strip binary**: Consider stripping debug symbols to reduce size (`arm-linux-gnueabihf-strip sKeets`)
4. **Bundle Qt libs**: The binary links dynamically to Qt; ensure Qt armhf shared libs are on the Kobo or bundle them in the package

---

## Key Technical Details

- **carlonluca/qt-dev Docker images**: Investigated, only ship amd64 and arm64 — NO armhf. Can't use for Kobo.
- **SDK**: mfnboer/atproto (pinned SHA `2a1019bbe1e15a32b8ae678378c7adaf7e5dbf75`), fetched via CMake FetchContent
- **Original problem**: 3 categories of linker errors — ASAN symbols, QDBus/ICU/libproxy transitive deps, GLX dispatch symbols
- **Root cause**: `CMAKE_SYSROOT` + Ubuntu 24.04 Qt6 6.4.2 packages missing transitive deps in armhf sysroot context
- **Solution approach**: Build Qt 6.5.2 from source with minimal deps, drop CMAKE_SYSROOT, patch SDK

---

## How to Resume

```bash
# Check if build 4 finished:
tail -50 /tmp/skeets-build4.log

# If it failed, check errors:
grep -i "error\|FAILED" /tmp/skeets-build4.log | tail -20

# To trigger a fresh build:
cd /Users/evelyn/projects/sKeets
./docker-build.sh 2>&1 | tee /tmp/skeets-build5.log

# To rebuild only (Docker image cached, just re-run sKeets build):
# The docker-build.sh script runs cmake + ninja in a new container each time.
# To avoid a full Qt rebuild, don't modify Dockerfile.cross.

# Check Docker cache status:
docker images skeets-cross
```
