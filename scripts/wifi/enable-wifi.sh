#!/bin/sh

. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/common.sh"

require_wifi_env || exit $?

POWER_TOGGLE="module"
WPA_SUPPLICANT_DRIVER="wext"
KMOD_PATH="/drivers/${PLATFORM}/wifi"

case "${WIFI_MODULE}" in
    moal)
        POWER_TOGGLE="ntx_io"
        WPA_SUPPLICANT_DRIVER="nl80211"
        ;;
    wlan_drv_gen4m)
        POWER_TOGGLE="wmt"
        WPA_SUPPLICANT_DRIVER="nl80211"
        KMOD_PATH="/drivers/${PLATFORM}/mt66xx"
        ;;
esac

insmod_asneeded() {
    kmod="$1"
    shift
    if ! grep -q "^${kmod} " /proc/modules; then
        insmod "${KMOD_PATH}/${kmod}.ko" "$@"
        usleep 250000
    fi
}

log_wifi "Enabling Wi-Fi on interface ${INTERFACE} (platform=${PLATFORM}, module=${WIFI_MODULE})"

case "${POWER_TOGGLE}" in
    ntx_io)
        toggle_ntx_wifi 1 || exit $?
        ;;
    wmt)
        insmod_asneeded wmt_drv
        insmod_asneeded wmt_chrdev_wifi
        insmod_asneeded wmt_cdev_bt
        insmod_asneeded "${WIFI_MODULE}"
        echo "0xDB9DB9" >/proc/driver/wmt_dbg
        echo "7 9 0" >/proc/driver/wmt_dbg
        sleep 1
        echo "0xDB9DB9" >/proc/driver/wmt_dbg
        echo "7 9 1" >/proc/driver/wmt_dbg
        echo 1 >/dev/wmtWifi
        ;;
    *)
        if ! grep -q "^sdio_wifi_pwr " /proc/modules; then
            if [ -e "${KMOD_PATH}/sdio_wifi_pwr.ko" ]; then
                if [ -n "${CPUFREQ_DVFS}" ]; then
                    echo userspace >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
                    echo 1 >/sys/devices/platform/mxc_dvfs_core.0/enable
                fi
                insmod "${KMOD_PATH}/sdio_wifi_pwr.ko"
            else
                toggle_ntx_wifi 1 || exit $?
            fi
        fi
        ;;
esac

usleep 250000

if ! grep -q "^${WIFI_MODULE} " /proc/modules; then
    if [ -e "${KMOD_PATH}/${WIFI_MODULE}.ko" ]; then
        insmod "${KMOD_PATH}/${WIFI_MODULE}.ko"
    elif [ -e "/drivers/${PLATFORM}/${WIFI_MODULE}.ko" ]; then
        insmod "/drivers/${PLATFORM}/${WIFI_MODULE}.ko"
    else
        log_wifi "Wi-Fi module not found for ${WIFI_MODULE}"
        exit 1
    fi
fi

case "${WIFI_MODULE}" in
    moal) sleep 2 ;;
    *) sleep 1 ;;
esac

ifconfig "${INTERFACE}" up
[ "${WIFI_MODULE}" = "dhd" ] && wlarm_le -i "${INTERFACE}" up

pkill -0 wpa_supplicant || \
    wpa_supplicant -D "${WPA_SUPPLICANT_DRIVER}" -s -i "${INTERFACE}" \
        -c /etc/wpa_supplicant/wpa_supplicant.conf -C /var/run/wpa_supplicant -B

log_wifi "Wi-Fi enabled"