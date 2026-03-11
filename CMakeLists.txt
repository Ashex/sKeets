cmake_minimum_required(VERSION 3.16)
project(sKeets VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

add_compile_options(
    -Wall -Wextra -Wshadow
    -Wno-unused-parameter
    -ffunction-sections -fdata-sections
    -Os
)

if(CMAKE_CROSSCOMPILING)
    add_compile_options(-mcpu=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Qml)
qt_standard_project_setup()

get_target_property(QT6_CORE_IMPORTED_LOCATION Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT QT6_CORE_IMPORTED_LOCATION)
    get_target_property(QT6_CORE_IMPORTED_LOCATION Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(SKEETS_QT_LIB_DIR "${QT6_CORE_IMPORTED_LOCATION}" DIRECTORY)
get_filename_component(SKEETS_QT_ROOT_DIR "${SKEETS_QT_LIB_DIR}" DIRECTORY)
set(SKEETS_QT_PLUGIN_DIR "${SKEETS_QT_ROOT_DIR}/plugins")

function(skeets_resolve_toolchain_lib out_var lib_name)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=${lib_name}
        OUTPUT_VARIABLE resolved_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT resolved_path OR resolved_path STREQUAL lib_name)
        message(FATAL_ERROR "Failed to resolve toolchain library: ${lib_name}")
    endif()
    set(${out_var} "${resolved_path}" PARENT_SCOPE)
endfunction()

skeets_resolve_toolchain_lib(SKEETS_LD_LINUX ld-linux-armhf.so.3)
skeets_resolve_toolchain_lib(SKEETS_LIBC libc.so.6)
skeets_resolve_toolchain_lib(SKEETS_LIBM libm.so.6)
skeets_resolve_toolchain_lib(SKEETS_LIBSTDCPP libstdc++.so.6)
skeets_resolve_toolchain_lib(SKEETS_LIBGCC_S libgcc_s.so.1)
skeets_resolve_toolchain_lib(SKEETS_LIBZ libz.so.1)
skeets_resolve_toolchain_lib(SKEETS_LIBSSL libssl.so.3)
skeets_resolve_toolchain_lib(SKEETS_LIBCRYPTO libcrypto.so.3)

set(SKEETS_SYSROOT_LIB_DIR "")
if(DEFINED ENV{KOBO_SYSROOT} AND EXISTS "$ENV{KOBO_SYSROOT}/usr/lib")
    set(SKEETS_SYSROOT_LIB_DIR "$ENV{KOBO_SYSROOT}/usr/lib")
elseif(CMAKE_SYSROOT AND EXISTS "${CMAKE_SYSROOT}/usr/lib")
    set(SKEETS_SYSROOT_LIB_DIR "${CMAKE_SYSROOT}/usr/lib")
endif()

set(SKEETS_GLIBC_LOCALE_DIR "")
if(EXISTS "/usr/lib/locale/C.utf8")
    set(SKEETS_GLIBC_LOCALE_DIR "/usr/lib/locale/C.utf8")
endif()

set(SKEETS_CA_CERT_FILE "")
if(EXISTS "/etc/ssl/certs/ca-certificates.crt")
    set(SKEETS_CA_CERT_FILE "/etc/ssl/certs/ca-certificates.crt")
endif()

include(FetchContent)

FetchContent_Declare(
    atproto_sdk
    GIT_REPOSITORY https://github.com/mfnboer/atproto.git
    GIT_TAG        2a1019bbe1e15a32b8ae678378c7adaf7e5dbf75  # pinned SHA; no stable release tag available
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/patch-atproto.cmake
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(atproto_sdk)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        5736b15f7ea0ffb08dd38af21067c314d6a3aae9  # pinned SHA; stb uses commits not versioned releases
)
FetchContent_MakeAvailable(stb)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

# ── FBInk (e-ink framebuffer library) ──────────────────────────────────
include(ExternalProject)

if(CMAKE_CROSSCOMPILING)
    string(REGEX REPLACE "-g\\+\\+$" "" _FBINK_CROSS_TC "${CMAKE_CXX_COMPILER}")
    set(_FBINK_BUILD_CMD
        ${CMAKE_COMMAND} -E env "CROSS_TC=${_FBINK_CROSS_TC}"
        make -j4 staticlib KOBO=1 IMAGE=1 OPENTYPE=1
    )
else()
    set(_FBINK_BUILD_CMD make -j4 staticlib LINUX=1 IMAGE=1 OPENTYPE=1)
endif()

ExternalProject_Add(fbink_ext
    GIT_REPOSITORY    https://github.com/NiLuJe/FBInk.git
    GIT_TAG           ede0a8d0a107768609fde9a68aab5f5c583be398
    GIT_SUBMODULES    font8x8 stb libunibreak i2c-tools
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ${_FBINK_BUILD_CMD}
    INSTALL_COMMAND   ""
    BUILD_IN_SOURCE   TRUE
    BUILD_BYPRODUCTS  <SOURCE_DIR>/Release/libfbink.a
)

ExternalProject_Get_Property(fbink_ext SOURCE_DIR)
set(FBINK_SOURCE_DIR ${SOURCE_DIR})
file(MAKE_DIRECTORY ${FBINK_SOURCE_DIR})

add_library(fbink STATIC IMPORTED GLOBAL)
set_target_properties(fbink PROPERTIES
    IMPORTED_LOCATION ${FBINK_SOURCE_DIR}/Release/libfbink.a
    INTERFACE_INCLUDE_DIRECTORIES ${FBINK_SOURCE_DIR}
)
add_dependencies(fbink fbink_ext)

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/atproto/atproto_client.cpp
    src/ui/fb.cpp
    src/ui/font.cpp
    src/ui/input.cpp
    src/ui/auth_view.cpp
    src/ui/feed_view.cpp
    src/ui/thread_view.cpp
    src/ui/settings_view.cpp
    src/util/str.cpp
    src/util/config.cpp
    src/util/paths.cpp
    src/util/device.cpp
    src/util/image.cpp
    src/util/image_cache.cpp
)

add_executable(sKeets ${SOURCES})

target_include_directories(sKeets PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sKeets PRIVATE
    libatproto
    stb_image
    fbink
    Qt6::Core
    Qt6::Network
    Qt6::Qml
    m
)

target_link_options(sKeets PRIVATE -Wl,--gc-sections)

set(KOBO_INSTALL_PREFIX "mnt/onboard/.adds/sKeets")
file(GLOB SKEETS_FONT_ASSETS
    "${CMAKE_SOURCE_DIR}/*.ttf"
    "${CMAKE_SOURCE_DIR}/*.otf"
    "${CMAKE_SOURCE_DIR}/*.ttc"
)

install(TARGETS sKeets RUNTIME DESTINATION ${KOBO_INSTALL_PREFIX})
install(PROGRAMS run.sh DESTINATION ${KOBO_INSTALL_PREFIX})
if(SKEETS_FONT_ASSETS)
    install(FILES ${SKEETS_FONT_ASSETS} DESTINATION ${KOBO_INSTALL_PREFIX}/fonts)
endif()

add_custom_target(kobo-package
    COMMAND ${CMAKE_COMMAND} --install . --prefix kobo-pkg
    COMMAND ${CMAKE_COMMAND}
            -DDEST_LIB_DIR=${CMAKE_BINARY_DIR}/kobo-pkg/mnt/onboard/.adds/lib
            -DDEST_APP_DIR=${CMAKE_BINARY_DIR}/kobo-pkg/mnt/onboard/.adds/sKeets
            -DATPROTO_LIB=${CMAKE_BINARY_DIR}/_deps/atproto_sdk-build/lib/liblibatproto.so
            -DQT_LIB_DIR=${SKEETS_QT_LIB_DIR}
            -DQT_TLS_PLUGIN_DIR=${SKEETS_QT_PLUGIN_DIR}/tls
            -DSYSROOT_LIB_DIR=${SKEETS_SYSROOT_LIB_DIR}
            -DLD_LINUX_LIB=${SKEETS_LD_LINUX}
            -DLIBC_LIB=${SKEETS_LIBC}
            -DLIBM_LIB=${SKEETS_LIBM}
            -DLIBSTDCPP_LIB=${SKEETS_LIBSTDCPP}
            -DLIBGCC_S_LIB=${SKEETS_LIBGCC_S}
            -DLIBZ_LIB=${SKEETS_LIBZ}
            -DLIBSSL_LIB=${SKEETS_LIBSSL}
            -DLIBCRYPTO_LIB=${SKEETS_LIBCRYPTO}
            -DGLIBC_LOCALE_DIR=${SKEETS_GLIBC_LOCALE_DIR}
            -DCA_CERT_FILE=${SKEETS_CA_CERT_FILE}
            -P ${CMAKE_SOURCE_DIR}/cmake/package-runtime-libs.cmake
    COMMAND ${CMAKE_COMMAND} -E rm -rf kobo-pkg/bin
    COMMAND tar czf KoboRoot.tgz -C kobo-pkg .
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Packaging KoboRoot.tgz"
    DEPENDS sKeets
)
