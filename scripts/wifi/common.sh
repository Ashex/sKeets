#!/bin/sh

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_DIR="${SKEETS_DIR:-/mnt/onboard/.adds/sKeets}"
APP_LOG="${SKEETS_LOG:-${APP_DIR}/sKeets.log}"
INTERFACE="${SKEETS_INTERFACE:-${INTERFACE:-eth0}}"
PLATFORM="${SKEETS_PLATFORM:-${PLATFORM:-}}"
WIFI_MODULE="${SKEETS_WIFI_MODULE:-${WIFI_MODULE:-}}"
CPUFREQ_DVFS="${SKEETS_CPUFREQ_DVFS:-${CPUFREQ_DVFS:-}}"
CPUFREQ_CONSERVATIVE="${SKEETS_CPUFREQ_CONSERVATIVE:-${CPUFREQ_CONSERVATIVE:-}}"
APP_WIFI_HELPER="${SKEETS_WIFI_HELPER:-${APP_DIR}/bin/sKeets-tool}"

log_wifi() {
    echo "[$(date)] [$(basename "$0")] $*"
}

require_wifi_env() {
    if [ -z "${PLATFORM}" ]; then
        log_wifi "PLATFORM is unset; export SKEETS_PLATFORM before running this script"
        return 2
    fi
    if [ -z "${WIFI_MODULE}" ]; then
        log_wifi "WIFI_MODULE is unset; export SKEETS_WIFI_MODULE before running this script"
        return 2
    fi
    return 0
}

toggle_ntx_wifi() {
    state="$1"
    if [ -x "${APP_WIFI_HELPER}" ]; then
        "${APP_WIFI_HELPER}" ntx-io 208 "${state}"
        return $?
    fi
    log_wifi "ntx_io helper missing at ${APP_WIFI_HELPER}; cannot toggle via ntx_io"
    return 1
}

run_wifi_script() {
    script_name="$1"
    shift
    "${SCRIPT_DIR}/${script_name}" "$@"
}