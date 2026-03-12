#!/bin/sh

. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/common.sh"

restore_wifi() {
    log_wifi "Restarting Wi-Fi"

    run_wifi_script enable-wifi.sh || return $?

    wpac_timeout=0
    while ! wpa_cli status | grep -q "wpa_state=COMPLETED"; do
        if [ "${wpac_timeout}" -ge 60 ]; then
            log_wifi "Failed to connect to preferred AP"
            run_wifi_script disable-wifi.sh
            return 1
        fi
        usleep 250000
        wpac_timeout=$((wpac_timeout + 1))
    done

    run_wifi_script obtain-ip.sh || return $?

    log_wifi "Restarted Wi-Fi"
}

restore_wifi &