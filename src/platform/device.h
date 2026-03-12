#pragma once

#include <string>

struct skeets_device_info_t {
    std::string version_string;
    std::string product_id;
    std::string product_name;
    std::string codename;
    std::string platform;
    std::string wifi_module;
    std::string interface_name;
    std::string battery_sysfs;
    std::string touch_protocol;
    bool is_mtk = false;
    bool is_sunxi = false;
    bool is_color = false;
    bool is_smp = false;
};

skeets_device_info_t skeets_probe_device();
