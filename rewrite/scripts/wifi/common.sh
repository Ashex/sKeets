#!/bin/sh

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REWRITE_DIR="${SKEETS_REWRITE_DIR:-/mnt/onboard/.adds/sKeets-rewrite}"
REWRITE_LOG="${SKEETS_REWRITE_LOG:-${REWRITE_DIR}/sKeets-rewrite.log}"
INTERFACE="${SKEETS_REWRITE_INTERFACE:-${INTERFACE:-eth0}}"
PLATFORM="${SKEETS_REWRITE_PLATFORM:-${PLATFORM:-}}"
WIFI_MODULE="${SKEETS_REWRITE_WIFI_MODULE:-${WIFI_MODULE:-}}"
CPUFREQ_DVFS="${SKEETS_REWRITE_CPUFREQ_DVFS:-${CPUFREQ_DVFS:-}}"
CPUFREQ_CONSERVATIVE="${SKEETS_REWRITE_CPUFREQ_CONSERVATIVE:-${CPUFREQ_CONSERVATIVE:-}}"
REWRITE_WIFI_HELPER="${SKEETS_REWRITE_WIFI_HELPER:-${REWRITE_DIR}/bin/sKeets-rewrite-tool}"

log_wifi() {
    echo "[$(date)] [$(basename "$0")] $*"
}

require_wifi_env() {
    if [ -z "${PLATFORM}" ]; then
        log_wifi "PLATFORM is unset; export SKEETS_REWRITE_PLATFORM before running this script"
        return 2
    fi
    if [ -z "${WIFI_MODULE}" ]; then
        log_wifi "WIFI_MODULE is unset; export SKEETS_REWRITE_WIFI_MODULE before running this script"
        return 2
    fi
    return 0
}

toggle_ntx_wifi() {
    state="$1"
    if [ -x "${REWRITE_WIFI_HELPER}" ]; then
        "${REWRITE_WIFI_HELPER}" ntx-io 208 "${state}"
        return $?
    fi
    log_wifi "ntx_io helper missing at ${REWRITE_WIFI_HELPER}; cannot toggle via ntx_io"
    return 1
}

run_wifi_script() {
    script_name="$1"
    shift
    "${SCRIPT_DIR}/${script_name}" "$@"
}