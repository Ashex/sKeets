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
