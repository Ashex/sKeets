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
file(MAKE_DIRECTORY "${DEST_APP_DIR}/locale")
file(MAKE_DIRECTORY "${DEST_APP_DIR}/ssl/certs")

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
    "${SYSROOT_LIB_DIR}/libicudata.so.70"
    "${SYSROOT_LIB_DIR}/libicui18n.so.70"
    "${SYSROOT_LIB_DIR}/libicuuc.so.70"
    "${SYSROOT_LIB_DIR}/libzstd.so.1"
    "${SYSROOT_LIB_DIR}/libglib-2.0.so.0"
    "${SYSROOT_LIB_DIR}/libpcre.so.1"
    "${SYSROOT_LIB_DIR}/libpcre2-16.so.0"
)

foreach(lib IN LISTS runtime_libs)
    if(NOT lib)
        continue()
    endif()
    if(NOT EXISTS "${lib}")
        message(FATAL_ERROR "Required runtime library not found: ${lib}")
    endif()
    file(REAL_PATH "${lib}" resolved_lib)
    get_filename_component(dest_name "${lib}" NAME)
    file(COPY_FILE "${resolved_lib}" "${DEST_LIB_DIR}/${dest_name}" ONLY_IF_DIFFERENT)
endforeach()

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
