# ARM cross-compilation toolchain for Kobo Libra Colour (also compatible with Clara Colour)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=toolchain/arm-kobo-linux-gnueabihf.cmake ..

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ── Toolchain binaries ────────────────────────────────────────────
# Assumes arm-linux-gnueabihf-gcc is on $PATH, or set KOBO_TOOLCHAIN_PREFIX.
if(DEFINED ENV{KOBO_TOOLCHAIN_PREFIX})
    set(TOOLCHAIN_PREFIX "$ENV{KOBO_TOOLCHAIN_PREFIX}/bin/arm-linux-gnueabihf-")
else()
    set(TOOLCHAIN_PREFIX "arm-linux-gnueabihf-")
endif()

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_AR           "${TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB       "${TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_STRIP        "${TOOLCHAIN_PREFIX}strip")

# ── Optional Kobo sysroot ─────────────────────────────────────────
# Set KOBO_SYSROOT environment variable to point to a Kobo rootfs for
# finding device-specific libraries.  We use CMAKE_FIND_ROOT_PATH (not
# CMAKE_SYSROOT) so the compiler/linker don't get --sysroot which would
# redirect all search paths away from the Docker image's libraries.
if(DEFINED ENV{KOBO_SYSROOT})
    list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{KOBO_SYSROOT}")
endif()

# ── Target CPU: ARM Cortex-A35 ────────────────────────────────────
# Kobo Libra Colour / Clara Colour: i.MX 8ULP SoC, Cortex-A35
set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard -mthumb"
    CACHE STRING "Initial C flags for Kobo cross-compilation" FORCE)

# ── Search policies ───────────────────────────────────────────────
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)  # Use host programs (cmake, pkg-config)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)   # Libraries from sysroot first, then Docker image
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)   # Headers  from sysroot first, then Docker image
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
