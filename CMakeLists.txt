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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Quick QuickControls2)
qt_standard_project_setup()

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
        make -j4 staticlib KOBO=1 MINIMAL=1 IMAGE=1 OPENTYPE=1
    )
else()
    set(_FBINK_BUILD_CMD make -j4 staticlib LINUX=1 MINIMAL=1 IMAGE=1 OPENTYPE=1)
endif()

ExternalProject_Add(fbink_ext
    GIT_REPOSITORY    https://github.com/NiLuJe/FBInk.git
    GIT_TAG           ede0a8d0a107768609fde9a68aab5f5c583be398
    GIT_SUBMODULES    stb libunibreak i2c-tools
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
    src/ui/login_view.cpp
    src/ui/feed_view.cpp
    src/ui/thread_view.cpp
    src/ui/compose_view.cpp
    src/ui/settings_view.cpp
    src/util/str.cpp
    src/util/config.cpp
    src/util/image.cpp
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
    Qt6::Quick
    Qt6::QuickControls2
    m
)

target_link_options(sKeets PRIVATE -Wl,--gc-sections)

set(KOBO_INSTALL_PREFIX "mnt/onboard/.adds/sKeets")
install(TARGETS sKeets RUNTIME DESTINATION ${KOBO_INSTALL_PREFIX})

add_custom_target(kobo-package
    COMMAND ${CMAKE_COMMAND} --install . --prefix kobo-pkg
    COMMAND tar czf sKeets-koboroot.tgz -C kobo-pkg .
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Packaging KoboRoot.tgz"
    DEPENDS sKeets
)
