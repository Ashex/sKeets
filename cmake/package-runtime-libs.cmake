if(NOT DEFINED DEST_LIB_DIR)
    message(FATAL_ERROR "DEST_LIB_DIR is required")
endif()

if(NOT DEFINED DEST_APP_DIR)
    message(FATAL_ERROR "DEST_APP_DIR is required")
endif()

if(NOT DEFINED ATPROTO_LIB)
    message(FATAL_ERROR "ATPROTO_LIB is required")
endif()

if(NOT DEFINED SYSROOT_LIB_DIR)
    message(FATAL_ERROR "SYSROOT_LIB_DIR is required")
endif()

if(NOT DEFINED QT_LIB_DIR)
    message(FATAL_ERROR "QT_LIB_DIR is required")
endif()

if(NOT DEFINED QT_TLS_PLUGIN_DIR)
    message(FATAL_ERROR "QT_TLS_PLUGIN_DIR is required")
endif()

foreach(required_var IN ITEMS LD_LINUX_LIB LIBC_LIB LIBM_LIB LIBSTDCPP_LIB LIBGCC_S_LIB LIBZ_LIB)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${DEST_LIB_DIR}")
file(MAKE_DIRECTORY "${DEST_LIB_DIR}")
file(MAKE_DIRECTORY "${DEST_APP_DIR}/plugins")

if(EXISTS "${DEST_APP_DIR}/lib" OR IS_SYMLINK "${DEST_APP_DIR}/lib")
    file(REMOVE_RECURSE "${DEST_APP_DIR}/lib")
endif()
file(MAKE_DIRECTORY "${DEST_APP_DIR}/lib")
file(MAKE_DIRECTORY "${DEST_APP_DIR}/locale")
file(MAKE_DIRECTORY "${DEST_APP_DIR}/ssl/certs")

function(copy_runtime_lib lib)
    if(NOT lib)
        return()
    endif()
    if(NOT EXISTS "${lib}")
        message(FATAL_ERROR "Required runtime library not found: ${lib}")
    endif()
    file(REAL_PATH "${lib}" resolved_lib)
    get_filename_component(dest_name "${lib}" NAME)
    file(COPY_FILE "${resolved_lib}" "${DEST_LIB_DIR}/${dest_name}" ONLY_IF_DIFFERENT)
endfunction()

set(RUNTIME_LIB_SEARCH_DIRS
    "${SYSROOT_LIB_DIR}"
    "${QT_LIB_DIR}"
)

foreach(base_lib IN ITEMS LD_LINUX_LIB LIBC_LIB LIBM_LIB LIBSTDCPP_LIB LIBGCC_S_LIB LIBZ_LIB LIBSSL_LIB LIBCRYPTO_LIB ATPROTO_LIB)
    if(DEFINED ${base_lib} AND ${base_lib})
        get_filename_component(base_lib_dir "${${base_lib}}" DIRECTORY)
        list(APPEND RUNTIME_LIB_SEARCH_DIRS "${base_lib_dir}")
    endif()
endforeach()

list(APPEND RUNTIME_LIB_SEARCH_DIRS
    "/lib/arm-linux-gnueabihf"
    "/usr/lib/arm-linux-gnueabihf"
    "/usr/arm-linux-gnueabihf/lib"
)
list(REMOVE_ITEM RUNTIME_LIB_SEARCH_DIRS "")
list(REMOVE_DUPLICATES RUNTIME_LIB_SEARCH_DIRS)

function(copy_optional_runtime_lib lib_name)
    find_file(resolved_optional_lib
        NAMES "${lib_name}"
        PATHS ${RUNTIME_LIB_SEARCH_DIRS}
        NO_DEFAULT_PATH
    )
    if(resolved_optional_lib)
        copy_runtime_lib("${resolved_optional_lib}")
    else()
        message(STATUS "Optional runtime library not found, skipping: ${lib_name}")
    endif()
endfunction()

set(runtime_libs
    "${LD_LINUX_LIB}"
    "${LIBC_LIB}"
    "${LIBM_LIB}"
    "${LIBSTDCPP_LIB}"
    "${LIBGCC_S_LIB}"
    "${LIBZ_LIB}"
    "${LIBSSL_LIB}"
    "${LIBCRYPTO_LIB}"
    "${ATPROTO_LIB}"
    "${QT_LIB_DIR}/libQt6Core.so.6"
    "${QT_LIB_DIR}/libQt6Network.so.6"
    "${QT_LIB_DIR}/libQt6Qml.so.6"
)

foreach(lib IN LISTS runtime_libs)
    copy_runtime_lib("${lib}")
endforeach()

# These are transitive runtime dependencies that may be present depending on
# how Qt and the cross environment were built. GitHub Actions does not provide
# SYSROOT_LIB_DIR, so resolve them from known armhf library directories.
foreach(optional_lib IN ITEMS
    libicudata.so.70
    libicui18n.so.70
    libicuuc.so.70
    libzstd.so.1
    libglib-2.0.so.0
    libpcre.so.1
    libpcre2-16.so.0
)
    copy_optional_runtime_lib("${optional_lib}")
endforeach()

if(EXISTS "${DEST_LIB_DIR}/libssl.so")
    file(REMOVE "${DEST_LIB_DIR}/libssl.so")
endif()
if(EXISTS "${DEST_LIB_DIR}/libcrypto.so")
    file(REMOVE "${DEST_LIB_DIR}/libcrypto.so")
endif()
# Use real copies instead of symlinks — the Kobo's /mnt/onboard is VFAT
# which cannot represent symlinks, so tar extraction silently drops them.
file(COPY_FILE "${DEST_LIB_DIR}/libssl.so.3" "${DEST_LIB_DIR}/libssl.so" ONLY_IF_DIFFERENT)
file(COPY_FILE "${DEST_LIB_DIR}/libcrypto.so.3" "${DEST_LIB_DIR}/libcrypto.so" ONLY_IF_DIFFERENT)

file(COPY_FILE "${DEST_LIB_DIR}/libssl.so.3" "${DEST_APP_DIR}/lib/libssl.so.3" ONLY_IF_DIFFERENT)
file(COPY_FILE "${DEST_LIB_DIR}/libcrypto.so.3" "${DEST_APP_DIR}/lib/libcrypto.so.3" ONLY_IF_DIFFERENT)

if(EXISTS "${DEST_APP_DIR}/lib/libssl.so")
    file(REMOVE "${DEST_APP_DIR}/lib/libssl.so")
endif()
if(EXISTS "${DEST_APP_DIR}/lib/libcrypto.so")
    file(REMOVE "${DEST_APP_DIR}/lib/libcrypto.so")
endif()
file(COPY_FILE "${DEST_APP_DIR}/lib/libssl.so.3" "${DEST_APP_DIR}/lib/libssl.so" ONLY_IF_DIFFERENT)
file(COPY_FILE "${DEST_APP_DIR}/lib/libcrypto.so.3" "${DEST_APP_DIR}/lib/libcrypto.so" ONLY_IF_DIFFERENT)

if(NOT EXISTS "${QT_TLS_PLUGIN_DIR}")
    message(FATAL_ERROR "Qt TLS plugin directory not found: ${QT_TLS_PLUGIN_DIR}")
endif()
file(COPY "${QT_TLS_PLUGIN_DIR}" DESTINATION "${DEST_APP_DIR}/plugins")

if(DEFINED GLIBC_LOCALE_DIR AND GLIBC_LOCALE_DIR)
    if(NOT EXISTS "${GLIBC_LOCALE_DIR}")
        message(FATAL_ERROR "glibc locale directory not found: ${GLIBC_LOCALE_DIR}")
    endif()
    file(COPY "${GLIBC_LOCALE_DIR}" DESTINATION "${DEST_APP_DIR}/locale")
endif()

if(DEFINED CA_CERT_FILE AND CA_CERT_FILE)
    if(NOT EXISTS "${CA_CERT_FILE}")
        message(FATAL_ERROR "CA certificate bundle not found: ${CA_CERT_FILE}")
    endif()
    file(COPY_FILE "${CA_CERT_FILE}" "${DEST_APP_DIR}/ssl/certs/ca-certificates.crt" ONLY_IF_DIFFERENT)
endif()
