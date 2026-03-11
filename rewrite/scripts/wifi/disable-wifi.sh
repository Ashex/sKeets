#!/bin/sh

. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/common.sh"

require_wifi_env || exit $?

cp -a /etc/resolv.conf /tmp/resolv.ko
old_hash="$(md5sum /etc/resolv.conf | cut -f1 -d' ')"

if [ -x /sbin/dhcpcd ]; then
    dhcpcd -d -k "${INTERFACE}"
    killall -q -TERM udhcpc default.script
else
    killall -q -TERM udhcpc default.script dhcpcd
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

wpa_cli -i "${INTERFACE}" terminate
[ "${WIFI_MODULE}" = "dhd" ] && wlarm_le -i "${INTERFACE}" down
ifconfig "${INTERFACE}" down

WIFI_DEP_MOD=""
POWER_TOGGLE="module"
SKIP_UNLOAD=""
case "${WIFI_MODULE}" in
    moal)
        WIFI_DEP_MOD="mlan"
        POWER_TOGGLE="ntx_io"
        ;;
    wlan_drv_gen4m)
        POWER_TOGGLE="wmt"
        SKIP_UNLOAD="true"
        ;;
esac

if [ -z "${SKIP_UNLOAD}" ]; then
    if grep -q "^${WIFI_MODULE} " /proc/modules; then
        usleep 250000
        rmmod "${WIFI_MODULE}"
    fi
    if [ -n "${WIFI_DEP_MOD}" ] && grep -q "^${WIFI_DEP_MOD} " /proc/modules; then
        usleep 250000
        rmmod "${WIFI_DEP_MOD}"
    fi
fi

case "${POWER_TOGGLE}" in
    ntx_io)
        usleep 250000
        toggle_ntx_wifi 0 || exit $?
        ;;
    wmt)
        echo 0 >/dev/wmtWifi
        ;;
    *)
        if grep -q "^sdio_wifi_pwr " /proc/modules; then
            if [ -n "${CPUFREQ_DVFS}" ]; then
                echo 0 >/sys/devices/platform/mxc_dvfs_core.0/enable
                if [ -n "${CPUFREQ_CONSERVATIVE}" ]; then
                    echo conservative >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
                else
                    echo userspace >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
                    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq >/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
                fi
            fi
            usleep 250000
            rmmod sdio_wifi_pwr
        fi
        if [ ! -e "/drivers/${PLATFORM}/wifi/sdio_wifi_pwr.ko" ]; then
            usleep 250000
            toggle_ntx_wifi 0 || exit $?
        fi
        ;;
esac

log_wifi "Wi-Fi disabled"