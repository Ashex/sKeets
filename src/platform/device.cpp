#include "platform/device.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

struct model_hint_t {
    const char* codename;
    const char* product_name;
    const char* product_id;
    bool is_mtk;
    bool is_color;
    bool is_smp;
    const char* battery_sysfs;
    const char* touch_protocol;
};

constexpr std::array<model_hint_t, 6> kModelHints{{
    {"spaColour", "Kobo Clara Colour", "393", true, true,  true,  "/sys/class/power_supply/bd71827_bat", "snow"},
    {"spaBW",     "Kobo Clara BW",     "391", true, false, false, "/sys/class/power_supply/bd71827_bat", "snow"},
    {"monza",     "Kobo Libra Colour", "390", true, true,  true,  "/sys/class/power_supply/bd71827_bat", "multitouch"},
    {"goldfinch", "Kobo Sage",         "387", false,false, true,  "/sys/class/power_supply/bd71827_bat", "multitouch"},
    {"condor",    "Kobo Elipsa 2E",    "389", false,false, true,  "/sys/class/power_supply/bd71827_bat", "multitouch"},
    {"europa",    "Kobo Libra 2",      "384", false,false, true,  "/sys/class/power_supply/bd71827_bat", "multitouch"},
}};

std::string trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

std::optional<std::string> read_first_line(const char* path) {
    std::ifstream file(path);
    if (!file) return std::nullopt;

    std::string line;
    std::getline(file, line);
    return trim(line);
}

bool path_exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

std::optional<int> find_pid_by_comm(const char* comm_name) {
    DIR* dir = opendir("/proc");
    if (!dir) return std::nullopt;

    while (dirent* entry = readdir(dir)) {
        if (!std::isdigit(static_cast<unsigned char>(entry->d_name[0]))) continue;
        const std::string pid = entry->d_name;
        const std::string comm_path = std::string("/proc/") + pid + "/comm";
        auto comm = read_first_line(comm_path.c_str());
        if (comm && *comm == comm_name) {
            closedir(dir);
            return std::stoi(pid);
        }
    }

    closedir(dir);
    return std::nullopt;
}

std::optional<std::string> read_env_from_pid(int pid, const char* key) {
    const std::string path = "/proc/" + std::to_string(pid) + "/environ";
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;

    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string prefix = std::string(key) + "=";

    size_t pos = 0;
    while (pos < data.size()) {
        const size_t end = data.find('\0', pos);
        const std::string_view entry(data.data() + pos, (end == std::string::npos ? data.size() : end) - pos);
        if (entry.substr(0, prefix.size()) == prefix) {
            return std::string(entry.substr(prefix.size()));
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }

    return std::nullopt;
}

std::optional<std::string> probe_process_env(const char* proc_name, const char* key) {
    auto pid = find_pid_by_comm(proc_name);
    if (!pid) return std::nullopt;
    return read_env_from_pid(*pid, key);
}

std::optional<std::string> run_command_trimmed(const char* command) {
    FILE* pipe = popen(command, "r");
    if (!pipe) return std::nullopt;

    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    const int status = pclose(pipe);
    if (status != 0) return std::nullopt;
    output = trim(output);
    if (output.empty()) return std::nullopt;
    return output;
}

std::optional<std::string> detect_product_id_from_version(const std::string& version) {
    if (version.size() < 3) return std::nullopt;

    std::string digits;
    for (auto it = version.rbegin(); it != version.rend(); ++it) {
        if (std::isdigit(static_cast<unsigned char>(*it))) {
            digits.push_back(*it);
            if (digits.size() == 3) break;
        } else if (!digits.empty()) {
            break;
        }
    }
    if (digits.size() != 3) return std::nullopt;
    std::reverse(digits.begin(), digits.end());
    return digits;
}

const model_hint_t* find_hint_by_codename(const std::string& codename) {
    for (const auto& hint : kModelHints) {
        if (codename == hint.codename) return &hint;
    }
    return nullptr;
}

const model_hint_t* find_hint_by_product_id(const std::string& product_id) {
    for (const auto& hint : kModelHints) {
        if (product_id == hint.product_id) return &hint;
    }
    return nullptr;
}

std::string detect_platform_from_files(const skeets_device_info_t& info) {
    if (info.is_mtk) {
        if (path_exists("/drivers/b300-ntx/mt66xx/wlan_drv_gen4m.ko")) return "b300-ntx";
        if (path_exists("/drivers/mt8113t-ntx/mt66xx/wlan_drv_gen4m.ko")) return "mt8113t-ntx";
    }
    if (path_exists("/etc/u-boot/b300-ntx/u-boot.mmc")) return "b300-ntx";
    if (path_exists("/etc/u-boot/ntx508/u-boot.mmc")) return "ntx508";
    if (path_exists("/etc/u-boot/freescale/u-boot.mmc")) return "freescale";
    return info.is_mtk ? "b300-ntx" : "freescale";
}

std::string detect_wifi_module(const skeets_device_info_t& info) {
    if (path_exists("/drivers/" + info.platform + "/mt66xx/wlan_drv_gen4m.ko")) return "wlan_drv_gen4m";
    if (path_exists("/drivers/" + info.platform + "/wifi/moal.ko")) return "moal";
    if (path_exists("/drivers/" + info.platform + "/wifi/8821cs.ko")) return "8821cs";
    if (info.is_mtk) return "wlan_drv_gen4m";
    return {};
}

} // namespace

skeets_device_info_t skeets_probe_device() {
    skeets_device_info_t info;

    if (const char* env_product = std::getenv("PRODUCT")) info.codename = env_product;
    if (const char* env_platform = std::getenv("PLATFORM")) info.platform = env_platform;
    if (const char* env_wifi_module = std::getenv("WIFI_MODULE")) info.wifi_module = env_wifi_module;
    if (const char* env_interface = std::getenv("INTERFACE")) info.interface_name = env_interface;

    if (auto version = read_first_line("/mnt/onboard/.kobo/version")) {
        info.version_string = *version;
    }

    if (info.codename.empty()) {
        if (auto product = probe_process_env("udevd", "PRODUCT")) info.codename = *product;
    }
    if (info.codename.empty()) {
        if (auto product = run_command_trimmed("/bin/kobo_config.sh 2>/dev/null")) info.codename = *product;
    }

    if (auto product_id = detect_product_id_from_version(info.version_string)) {
        info.product_id = *product_id;
    }

    if (info.platform.empty()) {
        if (auto platform = probe_process_env("udevd", "PLATFORM")) info.platform = *platform;
    }
    if (info.wifi_module.empty()) {
        if (auto wifi_module = probe_process_env("nickel", "WIFI_MODULE")) info.wifi_module = *wifi_module;
    }
    if (info.interface_name.empty()) {
        if (auto interface_name = probe_process_env("nickel", "INTERFACE")) info.interface_name = *interface_name;
    }
    if (info.interface_name.empty()) info.interface_name = "eth0";

    const model_hint_t* hint = nullptr;
    if (!info.codename.empty()) hint = find_hint_by_codename(info.codename);
    if (!hint && !info.product_id.empty()) hint = find_hint_by_product_id(info.product_id);

    if (hint) {
        if (info.codename.empty()) info.codename = hint->codename;
        info.product_name = hint->product_name;
        if (info.product_id.empty()) info.product_id = hint->product_id;
        info.is_mtk = hint->is_mtk;
        info.is_color = hint->is_color;
        info.is_smp = hint->is_smp;
        info.battery_sysfs = hint->battery_sysfs;
        info.touch_protocol = hint->touch_protocol;
    }

    if (info.codename.empty()) info.codename = "unknown";
    if (info.product_name.empty()) {
        if (!info.product_id.empty()) info.product_name = "Unknown Kobo (" + info.product_id + ")";
        else info.product_name = "Unknown Kobo";
    }

    if (info.battery_sysfs.empty()) {
        if (path_exists("/sys/class/power_supply/battery")) info.battery_sysfs = "/sys/class/power_supply/battery";
        else if (path_exists("/sys/class/power_supply/bd71827_bat")) info.battery_sysfs = "/sys/class/power_supply/bd71827_bat";
        else info.battery_sysfs = "/sys/class/power_supply/mc13892_bat";
    }

    if (info.touch_protocol.empty()) {
        info.touch_protocol = info.is_mtk ? "snow" : "multitouch";
    }

    if (info.platform.empty()) info.platform = detect_platform_from_files(info);
    if (info.wifi_module.empty()) info.wifi_module = detect_wifi_module(info);
    info.is_sunxi = info.platform == "b300-ntx";

    return info;
}
