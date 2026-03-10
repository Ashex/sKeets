#!/bin/sh
# Launcher script for sKeets on Kobo.
# Pauses Nickel so sKeets can own the framebuffer, then resumes it on exit.

SKEETS_DIR="/mnt/onboard/.adds/sKeets"

# Pause Nickel (the stock Kobo UI) so it stops drawing to the framebuffer.
killall -SIGSTOP nickel

# Give Nickel a moment to fully stop before we take over the display.
sleep 1

# Launch sKeets (blocks until the user exits).
"${SKEETS_DIR}/sKeets"

# Resume Nickel.
killall -SIGCONT nickel

#!/bin/sh
# Launcher script for sKeets on Kobo.
# Kills Nickel, feeds the hardware watchdog, reboots on exit.
# Designed to be launched from NickelMenu with cmd_spawn:quiet.

SKEETS_DIR="/mnt/onboard/.adds/sKeets"
LOG="${SKEETS_DIR}/sKeets.log"

echo "=== start $(date) ===" >>"$LOG"

# --- Watchdog feeder ---
# Once Nickel is dead nothing feeds the hardware watchdog, so the device
# will hard-reboot after ~60 s unless we keep it alive ourselves.
WD_PID=""
start_watchdog() {
    if [ -e /dev/watchdog ]; then
        ( exec 3>/dev/watchdog; while true; do printf '.' >&3; sleep 10; done ) &
        WD_PID=$!
    fi
}
stop_watchdog() {
    [ -n "$WD_PID" ] && kill "$WD_PID" 2>/dev/null && wait "$WD_PID" 2>/dev/null
    WD_PID=""
}

# --- Cleanup: reboot to bring Nickel back ---
cleanup() {
    echo "Cleanup at $(date), rebooting..." >>"$LOG"
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

rm -f /tmp/nickel-hardware-status
start_watchdog

# --- Launch sKeets ---
"${SKEETS_DIR}/sKeets" >>"$LOG" 2>&1

# cleanup runs automatically via EXIT trap
