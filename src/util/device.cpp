#include "device.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

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

} // namespace

std::string device_kobo_version_string() {
    std::ifstream file("/mnt/onboard/.kobo/version");
    if (!file) return {};

    std::string version;
    std::getline(file, version);
    return trim(version);
}

std::string device_kobo_model_string() {
    const std::string version = device_kobo_version_string();
    if (version.size() < 3) return "unknown";

    const std::string id = version.substr(version.size() - 3);
    if (id == "376") return "Kobo Clara HD";
    if (id == "377") return "Kobo Libra H2O";
    if (id == "384") return "Kobo Clara Colour";
    if (id == "389") return "Kobo Elipsa 2E";
    return std::string("Unknown Kobo (") + id + ")";
}
