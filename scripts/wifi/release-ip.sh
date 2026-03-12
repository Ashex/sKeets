#!/bin/sh

. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/common.sh"

cp -a /etc/resolv.conf /tmp/resolv.ko
old_hash="$(md5sum /etc/resolv.conf | cut -f1 -d' ')"

if [ -x /sbin/dhcpcd ]; then
    dhcpcd -d -k "${INTERFACE}"
    killall -q -TERM udhcpc default.script
else
    killall -q -TERM udhcpc default.script dhcpcd
    ifconfig "${INTERFACE}" 0.0.0.0
fi

kill_timeout=0
while pkill -0 udhcpc; do
    [ "${kill_timeout}" -ge 20 ] && break
    usleep 250000
    kill_timeout=$((kill_timeout + 1))
done

new_hash="$(md5sum /etc/resolv.conf | cut -f1 -d' ')"
if [ "${new_hash}" != "${old_hash}" ]; then
    mv -f /tmp/resolv.ko /etc/resolv.conf
else
    rm -f /tmp/resolv.ko
fi

log_wifi "Released IP for ${INTERFACE}"