#!/bin/sh

. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/common.sh"

log_wifi "Obtaining IP for ${INTERFACE}"
run_wifi_script release-ip.sh

if [ -x /sbin/dhcpcd ]; then
    dhcpcd -d -t 30 -w "${INTERFACE}"
else
    udhcpc -S -i "${INTERFACE}" -s /etc/udhcpc.d/default.script -b -q
fi