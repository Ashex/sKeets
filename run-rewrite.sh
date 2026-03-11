#!/bin/sh
# Rewrite launcher skeleton for sKeets on Kobo.
# Phase 1 purpose: own package paths, runtime environment, and diagnostics mode.

REWRITE_DIR="/mnt/onboard/.adds/sKeets-rewrite"
LOG="${REWRITE_DIR}/sKeets-rewrite.log"
LIB_DIR="/mnt/onboard/.adds/lib"
LOADER="${LIB_DIR}/ld-linux-armhf.so.3"
APP_LIBRARY_PATH="${LIB_DIR}:${REWRITE_DIR}/lib"
APP_PLUGIN_PATH="${REWRITE_DIR}/plugins"
APP_LOCALE_PATH="${REWRITE_DIR}/locale"
APP_CA_CERT_FILE="${REWRITE_DIR}/ssl/certs/ca-certificates.crt"
APP_SCRIPT_DIR="${REWRITE_DIR}/scripts"
REVISION_FILE="${REWRITE_DIR}/package-revision.txt"
DIAG_BIN="${REWRITE_DIR}/sKeets-rewrite-diag"
APP_BIN="${REWRITE_DIR}/sKeets-rewrite"
HELPER_BIN="${REWRITE_DIR}/bin/sKeets-rewrite-tool"
INHERITED_LD_LIBRARY_PATH="${LD_LIBRARY_PATH}"
MODE="${1:-${SKEETS_REWRITE_MODE:-diag}}"
APP_INTERFACE="${SKEETS_REWRITE_INTERFACE:-eth0}"
APP_PLATFORM="${SKEETS_REWRITE_PLATFORM:-}"
APP_WIFI_MODULE="${SKEETS_REWRITE_WIFI_MODULE:-}"
APP_PRODUCT_ID="${SKEETS_REWRITE_PRODUCT_ID:-}"
APP_PRODUCT_NAME="${SKEETS_REWRITE_PRODUCT_NAME:-}"
APP_CODENAME="${SKEETS_REWRITE_CODENAME:-}"
APP_BATTERY_SYSFS="${SKEETS_REWRITE_BATTERY_SYSFS:-}"
APP_TOUCH_PROTOCOL="${SKEETS_REWRITE_TOUCH_PROTOCOL:-}"
APP_TOUCH_MIRROR_X="${SKEETS_REWRITE_TOUCH_MIRROR_X:-}"
APP_TOUCH_MIRROR_Y="${SKEETS_REWRITE_TOUCH_MIRROR_Y:-}"
APP_IS_MTK="${SKEETS_REWRITE_IS_MTK:-0}"
APP_IS_SUNXI="${SKEETS_REWRITE_IS_SUNXI:-0}"
APP_IS_COLOR="${SKEETS_REWRITE_IS_COLOR:-0}"
APP_IS_SMP="${SKEETS_REWRITE_IS_SMP:-0}"
APP_CPUFREQ_DVFS="${SKEETS_REWRITE_CPUFREQ_DVFS:-}"
APP_CPUFREQ_CONSERVATIVE="${SKEETS_REWRITE_CPUFREQ_CONSERVATIVE:-}"
WD_PID=""

hydrate_probe_env() {
    if [ ! -x "${HELPER_BIN}" ]; then
        echo "Rewrite helper missing: ${HELPER_BIN}"
        return 0
    fi

    if [ -n "${APP_PLATFORM}" ] && [ -n "${APP_WIFI_MODULE}" ]; then
        return 0
    fi

    eval "$({
        LOCPATH="${APP_LOCALE_PATH}" \
        LANG=C.UTF-8 LC_ALL=C.UTF-8 LC_CTYPE=C.UTF-8 \
        LD_LIBRARY_PATH="${APP_LIBRARY_PATH}" \
        QT_PLUGIN_PATH="${APP_PLUGIN_PATH}" \
        SSL_CERT_FILE="${APP_CA_CERT_FILE}" \
            "${LOADER}" --library-path "${APP_LIBRARY_PATH}" "${HELPER_BIN}" probe-shell
    })"

    APP_INTERFACE="${SKEETS_REWRITE_INTERFACE:-${APP_INTERFACE}}"
    APP_PLATFORM="${SKEETS_REWRITE_PLATFORM:-${APP_PLATFORM}}"
    APP_WIFI_MODULE="${SKEETS_REWRITE_WIFI_MODULE:-${APP_WIFI_MODULE}}"
    APP_PRODUCT_ID="${SKEETS_REWRITE_PRODUCT_ID:-${APP_PRODUCT_ID}}"
    APP_PRODUCT_NAME="${SKEETS_REWRITE_PRODUCT_NAME:-${APP_PRODUCT_NAME}}"
    APP_CODENAME="${SKEETS_REWRITE_CODENAME:-${APP_CODENAME}}"
    APP_BATTERY_SYSFS="${SKEETS_REWRITE_BATTERY_SYSFS:-${APP_BATTERY_SYSFS}}"
    APP_TOUCH_PROTOCOL="${SKEETS_REWRITE_TOUCH_PROTOCOL:-${APP_TOUCH_PROTOCOL}}"
    APP_TOUCH_MIRROR_X="${SKEETS_REWRITE_TOUCH_MIRROR_X:-${APP_TOUCH_MIRROR_X}}"
    APP_TOUCH_MIRROR_Y="${SKEETS_REWRITE_TOUCH_MIRROR_Y:-${APP_TOUCH_MIRROR_Y}}"
    APP_IS_MTK="${SKEETS_REWRITE_IS_MTK:-${APP_IS_MTK}}"
    APP_IS_SUNXI="${SKEETS_REWRITE_IS_SUNXI:-${APP_IS_SUNXI}}"
    APP_IS_COLOR="${SKEETS_REWRITE_IS_COLOR:-${APP_IS_COLOR}}"
    APP_IS_SMP="${SKEETS_REWRITE_IS_SMP:-${APP_IS_SMP}}"
}

if [ -z "${APP_TOUCH_MIRROR_X}" ] && [ "${APP_CODENAME}" = "spaColour" ] && [ "${APP_TOUCH_PROTOCOL}" = "snow" ]; then
    APP_TOUCH_MIRROR_X=1
fi

if [ -z "${APP_TOUCH_MIRROR_Y}" ]; then
    APP_TOUCH_MIRROR_Y=0
fi

mkdir -p "${REWRITE_DIR}"
cd "${REWRITE_DIR}" || exit 1
exec >>"${LOG}" 2>&1

hydrate_probe_env

echo "=== rewrite start $(date) ==="
echo "Mode: ${MODE}"
echo "Inherited LD_LIBRARY_PATH: ${INHERITED_LD_LIBRARY_PATH}"
echo "Effective LD_LIBRARY_PATH: ${APP_LIBRARY_PATH}"
echo "Plugin path: ${APP_PLUGIN_PATH}"
echo "CA cert path: ${APP_CA_CERT_FILE}"
echo "Script path: ${APP_SCRIPT_DIR}"
echo "Loader path: ${LOADER}"
echo "Interface: ${APP_INTERFACE}"
echo "Platform: ${APP_PLATFORM:-<unset>}"
echo "Wi-Fi module: ${APP_WIFI_MODULE:-<unset>}"
echo "Product ID: ${APP_PRODUCT_ID:-<unset>}"
echo "Product name: ${APP_PRODUCT_NAME:-<unset>}"
echo "Codename: ${APP_CODENAME:-<unset>}"
echo "Battery sysfs: ${APP_BATTERY_SYSFS:-<unset>}"
echo "Touch protocol: ${APP_TOUCH_PROTOCOL:-<unset>}"
echo "Touch mirror X: ${APP_TOUCH_MIRROR_X:-0}"
echo "Touch mirror Y: ${APP_TOUCH_MIRROR_Y:-0}"
echo "Is MTK: ${APP_IS_MTK}"
echo "Is sunxi: ${APP_IS_SUNXI}"
echo "Is color: ${APP_IS_COLOR}"
echo "Is SMP: ${APP_IS_SMP}"

for candidate in \
    "${LIB_DIR}/libssl.so" \
    "${LIB_DIR}/libssl.so.3" \
    "${LIB_DIR}/libcrypto.so" \
    "${LIB_DIR}/libcrypto.so.3" \
    "${REWRITE_DIR}/lib/libssl.so" \
    "${REWRITE_DIR}/lib/libssl.so.3" \
    "${REWRITE_DIR}/lib/libcrypto.so" \
    "${REWRITE_DIR}/lib/libcrypto.so.3"
do
    if [ -e "${candidate}" ] || [ -L "${candidate}" ]; then
        echo "Runtime lib: $(ls -ld "${candidate}")"
    else
        echo "Runtime lib missing: ${candidate}"
    fi
done

if [ -r "${REVISION_FILE}" ]; then
    while IFS= read -r line; do
        echo "Revision: ${line}"
    done <"${REVISION_FILE}"
fi

if [ -r /mnt/onboard/.kobo/version ]; then
    VERSION_LINE=$(tr -d '\r\n' </mnt/onboard/.kobo/version)
    echo "Kobo version: ${VERSION_LINE}"
fi

start_watchdog() {
    if [ -e /dev/watchdog ]; then
        (
            exec 3>/dev/watchdog || exit 0
            while true; do
                printf '.' >&3 || exit 0
                sleep 10
            done
        ) 2>/dev/null &
        WD_PID=$!
    fi
}

stop_watchdog() {
    [ -n "${WD_PID}" ] && kill "${WD_PID}" 2>/dev/null && wait "${WD_PID}" 2>/dev/null
    WD_PID=""
}

cleanup_app_mode() {
    echo "Rewrite cleanup at $(date), rebooting..."
    stop_watchdog
    sync
    reboot
}

run_script_mode() {
    SCRIPT_NAME="$1"
    SCRIPT_PATH="${APP_SCRIPT_DIR}/wifi/${SCRIPT_NAME}"

    if [ ! -x "${SCRIPT_PATH}" ]; then
        echo "Rewrite script not found or not executable: ${SCRIPT_PATH}"
        return 127
    fi

    echo "Launching rewrite script: ${SCRIPT_PATH}"
    SKEETS_REWRITE_DIR="${REWRITE_DIR}" \
    SKEETS_REWRITE_LOG="${LOG}" \
    SKEETS_REWRITE_INTERFACE="${APP_INTERFACE}" \
    SKEETS_REWRITE_PLATFORM="${APP_PLATFORM}" \
    SKEETS_REWRITE_WIFI_MODULE="${APP_WIFI_MODULE}" \
    SKEETS_REWRITE_PRODUCT_ID="${APP_PRODUCT_ID}" \
    SKEETS_REWRITE_PRODUCT_NAME="${APP_PRODUCT_NAME}" \
    SKEETS_REWRITE_CODENAME="${APP_CODENAME}" \
    SKEETS_REWRITE_BATTERY_SYSFS="${APP_BATTERY_SYSFS}" \
    SKEETS_REWRITE_TOUCH_PROTOCOL="${APP_TOUCH_PROTOCOL}" \
    SKEETS_REWRITE_TOUCH_MIRROR_X="${APP_TOUCH_MIRROR_X}" \
    SKEETS_REWRITE_TOUCH_MIRROR_Y="${APP_TOUCH_MIRROR_Y}" \
    SKEETS_REWRITE_IS_MTK="${APP_IS_MTK}" \
    SKEETS_REWRITE_IS_SUNXI="${APP_IS_SUNXI}" \
    SKEETS_REWRITE_IS_COLOR="${APP_IS_COLOR}" \
    SKEETS_REWRITE_IS_SMP="${APP_IS_SMP}" \
    SKEETS_REWRITE_CPUFREQ_DVFS="${APP_CPUFREQ_DVFS}" \
    SKEETS_REWRITE_CPUFREQ_CONSERVATIVE="${APP_CPUFREQ_CONSERVATIVE}" \
        "${SCRIPT_PATH}"
}

TARGET_BIN="${DIAG_BIN}"
NEEDS_FRAMEBUFFER=0
SCRIPT_MODE=""

case "${MODE}" in
    app)
        TARGET_BIN="${APP_BIN}"
        NEEDS_FRAMEBUFFER=1
        ;;
    diag)
        TARGET_BIN="${DIAG_BIN}"
        ;;
    fb-diag)
        TARGET_BIN="${DIAG_BIN}"
        NEEDS_FRAMEBUFFER=1
        ;;
    input-diag)
        TARGET_BIN="${DIAG_BIN}"
        NEEDS_FRAMEBUFFER=1
        ;;
    power-diag)
        TARGET_BIN="${DIAG_BIN}"
        NEEDS_FRAMEBUFFER=0
        ;;
    network-diag)
        TARGET_BIN="${DIAG_BIN}"
        NEEDS_FRAMEBUFFER=0
        ;;
    phase2-diag)
        TARGET_BIN="${DIAG_BIN}"
        NEEDS_FRAMEBUFFER=1
        ;;
    wifi-enable)
        SCRIPT_MODE="enable-wifi.sh"
        ;;
    wifi-disable)
        SCRIPT_MODE="disable-wifi.sh"
        ;;
    wifi-obtain-ip)
        SCRIPT_MODE="obtain-ip.sh"
        ;;
    wifi-release-ip)
        SCRIPT_MODE="release-ip.sh"
        ;;
    wifi-restore)
        SCRIPT_MODE="restore-wifi-async.sh"
        ;;
    *)
        echo "Unknown rewrite mode: ${MODE}"
        exit 64
        ;;
esac

if [ -n "${SCRIPT_MODE}" ]; then
    run_script_mode "${SCRIPT_MODE}"
    STATUS=$?
    echo "Rewrite script mode exited with status ${STATUS}"
    exit "${STATUS}"
fi

if [ ! -x "${TARGET_BIN}" ]; then
    echo "Target binary not found or not executable: ${TARGET_BIN}"
    if [ "${MODE}" = "app" ] && [ -x "${DIAG_BIN}" ]; then
        echo "Falling back to diagnostics mode"
        MODE="diag"
        TARGET_BIN="${DIAG_BIN}"
        NEEDS_FRAMEBUFFER=0
    else
        exit 127
    fi
fi

if [ "${NEEDS_FRAMEBUFFER}" -eq 1 ]; then
    trap cleanup_app_mode EXIT INT TERM
    sync
    killall -q -TERM nickel hindenburg sickel fickel adobehost fmon

    kill_wait=0
    while pkill -0 nickel 2>/dev/null; do
        [ "${kill_wait}" -ge 16 ] && break
        usleep 250000
        kill_wait=$((kill_wait + 1))
    done
    echo "Nickel stopped (waited ${kill_wait}x250ms)"
    rm -f /tmp/nickel-hardware-status
    start_watchdog
fi

echo "Launching rewrite target: ${TARGET_BIN}"
LOCPATH="${APP_LOCALE_PATH}" \
LANG=C.UTF-8 LC_ALL=C.UTF-8 LC_CTYPE=C.UTF-8 \
LD_LIBRARY_PATH="${APP_LIBRARY_PATH}" \
QT_PLUGIN_PATH="${APP_PLUGIN_PATH}" \
SSL_CERT_FILE="${APP_CA_CERT_FILE}" \
SKEETS_DATA_DIR="${REWRITE_DIR}" \
SKEETS_REWRITE_DIR="${REWRITE_DIR}" \
SKEETS_REWRITE_INTERFACE="${APP_INTERFACE}" \
SKEETS_REWRITE_PLATFORM="${APP_PLATFORM}" \
SKEETS_REWRITE_WIFI_MODULE="${APP_WIFI_MODULE}" \
SKEETS_REWRITE_PRODUCT_ID="${APP_PRODUCT_ID}" \
SKEETS_REWRITE_PRODUCT_NAME="${APP_PRODUCT_NAME}" \
SKEETS_REWRITE_CODENAME="${APP_CODENAME}" \
SKEETS_REWRITE_BATTERY_SYSFS="${APP_BATTERY_SYSFS}" \
SKEETS_REWRITE_TOUCH_PROTOCOL="${APP_TOUCH_PROTOCOL}" \
SKEETS_REWRITE_TOUCH_MIRROR_X="${APP_TOUCH_MIRROR_X}" \
SKEETS_REWRITE_TOUCH_MIRROR_Y="${APP_TOUCH_MIRROR_Y}" \
SKEETS_REWRITE_IS_MTK="${APP_IS_MTK}" \
SKEETS_REWRITE_IS_SUNXI="${APP_IS_SUNXI}" \
SKEETS_REWRITE_IS_COLOR="${APP_IS_COLOR}" \
SKEETS_REWRITE_IS_SMP="${APP_IS_SMP}" \
SKEETS_REWRITE_CPUFREQ_DVFS="${APP_CPUFREQ_DVFS}" \
SKEETS_REWRITE_CPUFREQ_CONSERVATIVE="${APP_CPUFREQ_CONSERVATIVE}" \
SKEETS_REWRITE_TARGET_BIN="${TARGET_BIN}" \
SKEETS_REWRITE_MODE="${MODE}" \
    "${LOADER}" --library-path "${APP_LIBRARY_PATH}" "${TARGET_BIN}"
STATUS=$?
echo "Rewrite target exited with status ${STATUS}"

if [ "${NEEDS_FRAMEBUFFER}" -eq 0 ]; then
    exit "${STATUS}"
fi

exit "${STATUS}"
