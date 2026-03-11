#pragma once

#include <string>

struct rewrite_power_info_t {
    std::string battery_sysfs;
    bool battery_present = false;
    int capacity_percent = -1;
    std::string status;
    bool is_charging = false;
    bool is_charged = false;
    bool can_standby = false;
    bool can_suspend = false;
    bool suspend_safe = false;
    std::string suspend_reason;
};

rewrite_power_info_t rewrite_probe_power();