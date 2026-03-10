# cmake/patch-atproto.cmake — Patch upstream atproto SDK for Qt 6.5 cross-compilation
#
# Fixes applied:
#   1. Disable ASAN/sanitizer flags from top-level CMakeLists.txt
#   2. Insert missing VERSION in qt_add_qml_module (required by Qt6)
#   3. Remove -Werror, -fsanitize, -fprofile-arcs from lib/CMakeLists.txt
#   4. Remove -lgcov link dependency from lib/CMakeLists.txt
#   5. Replace QString::slice() with chop() (slice is Qt 6.8+)
#   6. Fix QObject::moveToThread() void return (returns bool only in Qt 6.7+)
#   7. Fix Xrpc::Client constructor in test.h

# ── 0. Patch top-level CMakeLists.txt — disable ASAN ────────────────────────
# The SDK enables ASAN for all non-Android builds; this breaks cross-compilation.
file(READ CMakeLists.txt _top)
string(REPLACE
    "add_compile_options(-fsanitize=address)"
    "# add_compile_options(-fsanitize=address)  # disabled for cross-compilation"
    _top "${_top}"
)
string(REPLACE
    "add_compile_options(-fno-omit-frame-pointer)"
    "# add_compile_options(-fno-omit-frame-pointer)"
    _top "${_top}"
)
string(REPLACE
    "add_link_options(-fsanitize=address)"
    "# add_link_options(-fsanitize=address)"
    _top "${_top}"
)
file(WRITE CMakeLists.txt "${_top}")

# ── 1. Patch lib/CMakeLists.txt ──────────────────────────────────────────────
file(READ lib/CMakeLists.txt _content)

# 1a. Insert "VERSION 1.0" right after the module target name
string(REPLACE
    "qt_add_qml_module(libatproto\n    URI atproto.lib"
    "qt_add_qml_module(libatproto\n    VERSION 1.0\n    URI atproto.lib"
    _content "${_content}"
)

# 1b. Remove -Werror (shadow warnings in upstream code are not our problem)
string(REPLACE
    "add_compile_options(-Wall -Wextra -Werror)"
    "add_compile_options(-Wall -Wextra -Wno-shadow -Wno-implicit-fallthrough)"
    _content "${_content}"
)

# 1c. Disable ASAN and coverage flags block for cross-compilation
string(REPLACE
    "if (NOT ANDROID AND NOT CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)"
    "if (FALSE)  # disabled for cross-compilation"
    _content "${_content}"
)

# 1d. Remove -lgcov (coverage library not available in cross-compilation toolchain)
string(REPLACE
    "set(COVERAGE_LIB -lgcov)"
    "# set(COVERAGE_LIB -lgcov)  # disabled for cross-compilation"
    _content "${_content}"
)

file(WRITE lib/CMakeLists.txt "${_content}")

# ── 2. Patch lib/plc_directory_client.cpp — QString::slice() → chop() ────────
# QString::slice() was added in Qt 6.8; chop(1) is equivalent here and works on Qt 6.5+
file(READ lib/plc_directory_client.cpp _plc)
string(REPLACE
    "normalized.slice(0, normalized.size() - 1);"
    "normalized.chop(1);"
    _plc "${_plc}"
)
file(WRITE lib/plc_directory_client.cpp "${_plc}")

# ── 3. Patch lib/xrpc_client.cpp — moveToThread() returns void in Qt < 6.7 ──
file(READ lib/xrpc_client.cpp _xrpc)
string(REPLACE
    "if (mNetworkThread->moveToThread(mNetworkThread.get()))\n        qDebug() << \"Moved network thread\";\n    else\n        qWarning() << \"Failed to move network thread\";"
    "mNetworkThread->moveToThread(mNetworkThread.get());\n    qDebug() << \"Moved network thread\";"
    _xrpc "${_xrpc}"
)
file(WRITE lib/xrpc_client.cpp "${_xrpc}")

# ── 4. Patch test.h — Xrpc::Client constructor signature changed ────────────
file(READ test.h _test_h)
string(REPLACE
    "std::make_unique<Xrpc::Client>(mNetwork, host)"
    "std::make_unique<Xrpc::Client>(host)"
    _test_h "${_test_h}"
)
file(WRITE test.h "${_test_h}")
