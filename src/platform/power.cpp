#include "platform/power.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <sys/stat.h>

namespace {

std::string trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

bool path_exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

std::optional<std::string> read_first_line(const std::string& path) {
    std::ifstream file(path);
    if (!file) return std::nullopt;
    std::string line;
    std::getline(file, line);
    return trim(line);
}

std::optional<int> read_int_file(const std::string& path) {
    const auto line = read_first_line(path);
    if (!line) return std::nullopt;
    try {
        return std::stoi(*line);
    } catch (...) {
        return std::nullopt;
    }
}

bool power_state_supports(const char* token) {
    const auto state = read_first_line("/sys/power/state");
    return state && state->find(token) != std::string::npos;
}

bool env_flag_enabled(const char* key) {
    const char* value = std::getenv(key);
    return value && std::string(value) == "1";
}

std::string env_or_default(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    if (value && *value) return value;
    return fallback;
}

} // namespace

skeets_power_info_t skeets_probe_power() {
    skeets_power_info_t info;
    info.battery_sysfs = env_or_default("SKEETS_BATTERY_SYSFS", "/sys/class/power_supply/battery");
    info.battery_present = path_exists(info.battery_sysfs);

    if (info.battery_present) {
        if (const auto capacity = read_int_file(info.battery_sysfs + "/capacity")) {
            info.capacity_percent = *capacity;
        }

        if (const auto status = read_first_line(info.battery_sysfs + "/status")) {
            info.status = *status;
            info.is_charging = (*status == "Charging");
            info.is_charged = (*status == "Full" || *status == "Not charging");
        }
    }

    info.can_standby = power_state_supports("standby");
    info.can_suspend = path_exists("/sys/power/state-extended") && (power_state_supports("mem") || info.can_standby);

    const bool is_mtk = env_flag_enabled("SKEETS_IS_MTK");
    if (!info.can_suspend) {
        info.suspend_safe = false;
        info.suspend_reason = "kernel suspend interface unavailable";
    } else if (is_mtk && info.is_charging) {
        info.suspend_safe = false;
        info.suspend_reason = "unsafe on MTK while charging";
    } else {
        info.suspend_safe = true;
        info.suspend_reason = "safe under current baseline checks";
    }

    return info;
}