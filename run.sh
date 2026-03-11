#!/bin/sh
# Launcher script for sKeets on Kobo.
# Kills Nickel, feeds the hardware watchdog, reboots on exit.
# Designed to be launched from NickelMenu with cmd_spawn:quiet.

SKEETS_DIR="/mnt/onboard/.adds/sKeets"
LOG="${SKEETS_DIR}/sKeets.log"
LIB_DIR="/mnt/onboard/.adds/lib"
LOADER="${LIB_DIR}/ld-linux-armhf.so.3"
APP_LIBRARY_PATH="${LIB_DIR}:/usr/local/Kobo${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
APP_PLUGIN_PATH="${SKEETS_DIR}/plugins"
APP_LOCALE_PATH="${SKEETS_DIR}/locale"
APP_CA_CERT_FILE="${SKEETS_DIR}/ssl/certs/ca-certificates.crt"

exec >>"$LOG" 2>&1

echo "App library path: ${APP_LIBRARY_PATH}"

echo "=== start $(date) ==="
if [ -r /mnt/onboard/.kobo/version ]; then
    VERSION_LINE=$(tr -d '\r\n' </mnt/onboard/.kobo/version)
    echo "Kobo version: ${VERSION_LINE}"
fi

# --- Watchdog feeder ---
# Once Nickel is dead nothing feeds the hardware watchdog, so the device
# will hard-reboot after ~60 s unless we keep it alive ourselves.
WD_PID=""
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
    [ -n "$WD_PID" ] && kill "$WD_PID" 2>/dev/null && wait "$WD_PID" 2>/dev/null
    WD_PID=""
}

# --- Cleanup: reboot to bring Nickel back ---
cleanup() {
    echo "Cleanup at $(date), rebooting..."
    stop_watchdog
    sync
    reboot
}
trap cleanup EXIT INT TERM

# --- Kill Nickel and companion processes ---
sync
killall -q -TERM nickel hindenburg sickel fickel adobehost fmon

kill_wait=0
while pkill -0 nickel 2>/dev/null; do
    [ "$kill_wait" -ge 16 ] && break
    usleep 250000
    kill_wait=$((kill_wait + 1))
done
echo "Nickel stopped (waited ${kill_wait}x250ms)" >>"$LOG"
echo "Nickel stopped (waited ${kill_wait}x250ms)"

rm -f /tmp/nickel-hardware-status
start_watchdog

# --- Launch sKeets ---
echo "Launching ${SKEETS_DIR}/sKeets"
LOCPATH="${APP_LOCALE_PATH}" \
LANG=C.UTF-8 LC_ALL=C.UTF-8 LC_CTYPE=C.UTF-8 \
QT_PLUGIN_PATH="${APP_PLUGIN_PATH}" \
SSL_CERT_FILE="${APP_CA_CERT_FILE}" \
SKEETS_FONT_DIR="${SKEETS_DIR}/fonts" \
    "${LOADER}" --library-path "${APP_LIBRARY_PATH}" "${SKEETS_DIR}/sKeets" >>"$LOG" 2>&1
echo "sKeets exited with status $?"

# cleanup runs automatically via EXIT trap
