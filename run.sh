#!/bin/sh
# Main launcher for sKeets on Kobo.
# Owns package paths, runtime environment, and diagnostics/app mode dispatch.

APP_DIR="/mnt/onboard/.adds/sKeets"
LOG="${APP_DIR}/sKeets.log"
LIB_DIR="/mnt/onboard/.adds/lib"
LOADER="${LIB_DIR}/ld-linux-armhf.so.3"
APP_LIBRARY_PATH="${LIB_DIR}:${APP_DIR}/lib"
APP_PLUGIN_PATH="${APP_DIR}/plugins"
APP_LOCALE_PATH="${APP_DIR}/locale"
APP_CA_CERT_FILE="${APP_DIR}/ssl/certs/ca-certificates.crt"
APP_SCRIPT_DIR="${APP_DIR}/scripts"
REVISION_FILE="${APP_DIR}/package-revision.txt"
DIAG_BIN="${APP_DIR}/sKeets-diag"
APP_BIN="${APP_DIR}/sKeets"
HELPER_BIN="${APP_DIR}/bin/sKeets-tool"
INHERITED_LD_LIBRARY_PATH="${LD_LIBRARY_PATH}"
MODE="${1:-${SKEETS_MODE:-diag}}"
APP_INTERFACE="${SKEETS_INTERFACE:-eth0}"
APP_PLATFORM="${SKEETS_PLATFORM:-}"
APP_WIFI_MODULE="${SKEETS_WIFI_MODULE:-}"
APP_PRODUCT_ID="${SKEETS_PRODUCT_ID:-}"
APP_PRODUCT_NAME="${SKEETS_PRODUCT_NAME:-}"
APP_CODENAME="${SKEETS_CODENAME:-}"
APP_BATTERY_SYSFS="${SKEETS_BATTERY_SYSFS:-}"
APP_TOUCH_PROTOCOL="${SKEETS_TOUCH_PROTOCOL:-}"
APP_TOUCH_MIRROR_X="${SKEETS_TOUCH_MIRROR_X:-}"
APP_TOUCH_MIRROR_Y="${SKEETS_TOUCH_MIRROR_Y:-}"
APP_IS_MTK="${SKEETS_IS_MTK:-0}"
APP_IS_SUNXI="${SKEETS_IS_SUNXI:-0}"
APP_IS_COLOR="${SKEETS_IS_COLOR:-0}"
APP_IS_SMP="${SKEETS_IS_SMP:-0}"
APP_CPUFREQ_DVFS="${SKEETS_CPUFREQ_DVFS:-}"
APP_CPUFREQ_CONSERVATIVE="${SKEETS_CPUFREQ_CONSERVATIVE:-}"
WD_PID=""

hydrate_probe_env() {
    if [ ! -x "${HELPER_BIN}" ]; then
        echo "sKeets helper missing: ${HELPER_BIN}"
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

    APP_INTERFACE="${SKEETS_INTERFACE:-${APP_INTERFACE}}"
    APP_PLATFORM="${SKEETS_PLATFORM:-${APP_PLATFORM}}"
    APP_WIFI_MODULE="${SKEETS_WIFI_MODULE:-${APP_WIFI_MODULE}}"
    APP_PRODUCT_ID="${SKEETS_PRODUCT_ID:-${APP_PRODUCT_ID}}"
    APP_PRODUCT_NAME="${SKEETS_PRODUCT_NAME:-${APP_PRODUCT_NAME}}"
    APP_CODENAME="${SKEETS_CODENAME:-${APP_CODENAME}}"
    APP_BATTERY_SYSFS="${SKEETS_BATTERY_SYSFS:-${APP_BATTERY_SYSFS}}"
    APP_TOUCH_PROTOCOL="${SKEETS_TOUCH_PROTOCOL:-${APP_TOUCH_PROTOCOL}}"
    APP_TOUCH_MIRROR_X="${SKEETS_TOUCH_MIRROR_X:-${APP_TOUCH_MIRROR_X}}"
    APP_TOUCH_MIRROR_Y="${SKEETS_TOUCH_MIRROR_Y:-${APP_TOUCH_MIRROR_Y}}"
    APP_IS_MTK="${SKEETS_IS_MTK:-${APP_IS_MTK}}"
    APP_IS_SUNXI="${SKEETS_IS_SUNXI:-${APP_IS_SUNXI}}"
    APP_IS_COLOR="${SKEETS_IS_COLOR:-${APP_IS_COLOR}}"
    APP_IS_SMP="${SKEETS_IS_SMP:-${APP_IS_SMP}}"
}

mkdir -p "${APP_DIR}"
cd "${APP_DIR}" || exit 1
exec >>"${LOG}" 2>&1

hydrate_probe_env

# Clara Colour (spaColour/393): snow protocol, needs mirror_x
if [ -z "${APP_TOUCH_MIRROR_X}" ] && \
   { [ "${APP_CODENAME}" = "spaColour" ] || [ "${APP_PRODUCT_ID}" = "393" ]; } && \
   [ "${APP_TOUCH_PROTOCOL}" = "snow" ]; then
    APP_TOUCH_MIRROR_X=1
fi

# Libra Colour (monza/390): standard multitouch, needs mirror_y (not mirror_x)
if [ -z "${APP_TOUCH_MIRROR_Y}" ] && \
   { [ "${APP_CODENAME}" = "monza" ] || [ "${APP_PRODUCT_ID}" = "390" ]; }; then
    APP_TOUCH_MIRROR_Y=1
fi

# Elipsa 2E (condor/389): standard multitouch, needs mirror_y (not mirror_x)
if [ -z "${APP_TOUCH_MIRROR_Y}" ] && \
   { [ "${APP_CODENAME}" = "condor" ] || [ "${APP_PRODUCT_ID}" = "389" ]; }; then
    APP_TOUCH_MIRROR_Y=1
fi

if [ -z "${APP_TOUCH_MIRROR_X}" ]; then
    APP_TOUCH_MIRROR_X=0
fi

if [ -z "${APP_TOUCH_MIRROR_Y}" ]; then
    APP_TOUCH_MIRROR_Y=0
fi

echo "=== sKeets start $(date) ==="
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
    "${APP_DIR}/lib/libssl.so" \
    "${APP_DIR}/lib/libssl.so.3" \
    "${APP_DIR}/lib/libcrypto.so" \
    "${APP_DIR}/lib/libcrypto.so.3"
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
    echo "sKeets cleanup at $(date), rebooting..."
    stop_watchdog
    sync
    reboot
}

run_script_mode() {
    SCRIPT_NAME="$1"
    SCRIPT_PATH="${APP_SCRIPT_DIR}/wifi/${SCRIPT_NAME}"

    if [ ! -x "${SCRIPT_PATH}" ]; then
        echo "sKeets script not found or not executable: ${SCRIPT_PATH}"
        return 127
    fi

    echo "Launching sKeets script: ${SCRIPT_PATH}"
    SKEETS_DIR="${APP_DIR}" \
    SKEETS_LOG="${LOG}" \
    SKEETS_INTERFACE="${APP_INTERFACE}" \
    SKEETS_PLATFORM="${APP_PLATFORM}" \
    SKEETS_WIFI_MODULE="${APP_WIFI_MODULE}" \
    SKEETS_PRODUCT_ID="${APP_PRODUCT_ID}" \
    SKEETS_PRODUCT_NAME="${APP_PRODUCT_NAME}" \
    SKEETS_CODENAME="${APP_CODENAME}" \
    SKEETS_BATTERY_SYSFS="${APP_BATTERY_SYSFS}" \
    SKEETS_TOUCH_PROTOCOL="${APP_TOUCH_PROTOCOL}" \
    SKEETS_TOUCH_MIRROR_X="${APP_TOUCH_MIRROR_X}" \
    SKEETS_TOUCH_MIRROR_Y="${APP_TOUCH_MIRROR_Y}" \
    SKEETS_IS_MTK="${APP_IS_MTK}" \
    SKEETS_IS_SUNXI="${APP_IS_SUNXI}" \
    SKEETS_IS_COLOR="${APP_IS_COLOR}" \
    SKEETS_IS_SMP="${APP_IS_SMP}" \
    SKEETS_CPUFREQ_DVFS="${APP_CPUFREQ_DVFS}" \
    SKEETS_CPUFREQ_CONSERVATIVE="${APP_CPUFREQ_CONSERVATIVE}" \
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
        echo "Unknown sKeets mode: ${MODE}"
        exit 64
        ;;
esac

if [ -n "${SCRIPT_MODE}" ]; then
    run_script_mode "${SCRIPT_MODE}"
    STATUS=$?
    echo "sKeets script mode exited with status ${STATUS}"
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

echo "Launching sKeets target: ${TARGET_BIN}"
LOCPATH="${APP_LOCALE_PATH}" \
LANG=C.UTF-8 LC_ALL=C.UTF-8 LC_CTYPE=C.UTF-8 \
LD_LIBRARY_PATH="${APP_LIBRARY_PATH}" \
QT_PLUGIN_PATH="${APP_PLUGIN_PATH}" \
SSL_CERT_FILE="${APP_CA_CERT_FILE}" \
SKEETS_DATA_DIR="${APP_DIR}" \
SKEETS_DIR="${APP_DIR}" \
SKEETS_INTERFACE="${APP_INTERFACE}" \
SKEETS_PLATFORM="${APP_PLATFORM}" \
SKEETS_WIFI_MODULE="${APP_WIFI_MODULE}" \
SKEETS_PRODUCT_ID="${APP_PRODUCT_ID}" \
SKEETS_PRODUCT_NAME="${APP_PRODUCT_NAME}" \
SKEETS_CODENAME="${APP_CODENAME}" \
SKEETS_BATTERY_SYSFS="${APP_BATTERY_SYSFS}" \
SKEETS_TOUCH_PROTOCOL="${APP_TOUCH_PROTOCOL}" \
SKEETS_TOUCH_MIRROR_X="${APP_TOUCH_MIRROR_X}" \
SKEETS_TOUCH_MIRROR_Y="${APP_TOUCH_MIRROR_Y}" \
SKEETS_IS_MTK="${APP_IS_MTK}" \
SKEETS_IS_SUNXI="${APP_IS_SUNXI}" \
SKEETS_IS_COLOR="${APP_IS_COLOR}" \
SKEETS_IS_SMP="${APP_IS_SMP}" \
SKEETS_CPUFREQ_DVFS="${APP_CPUFREQ_DVFS}" \
SKEETS_CPUFREQ_CONSERVATIVE="${APP_CPUFREQ_CONSERVATIVE}" \
SKEETS_TARGET_BIN="${TARGET_BIN}" \
SKEETS_MODE="${MODE}" \
    "${LOADER}" --library-path "${APP_LIBRARY_PATH}" "${TARGET_BIN}"
STATUS=$?
echo "sKeets target exited with status ${STATUS}"

if [ "${NEEDS_FRAMEBUFFER}" -eq 0 ]; then
    exit "${STATUS}"
fi

exit "${STATUS}"
