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
