#include "rewrite/platform/network.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
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

bool read_int_flag(const std::string& path) {
    const auto line = read_first_line(path);
    if (!line) return false;
    return *line == "1";
}

std::string env_or_default(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    if (value && *value) return value;
    return fallback;
}

bool has_default_ipv4_route_for_interface(const std::string& interface_name) {
    std::ifstream routes("/proc/net/route");
    if (!routes) return false;

    std::string line;
    std::getline(routes, line);
    while (std::getline(routes, line)) {
        std::istringstream stream(line);
        std::string iface;
        std::string destination;
        std::string gateway;
        if (!(stream >> iface >> destination >> gateway)) continue;
        if (iface == interface_name && destination == "00000000") return true;
    }

    return false;
}

bool has_default_ipv6_route_for_interface(const std::string& interface_name) {
    std::ifstream routes("/proc/net/ipv6_route");
    if (!routes) return false;

    std::string line;
    while (std::getline(routes, line)) {
        std::istringstream stream(line);
        std::string destination;
        std::string dest_prefix_len;
        std::string source;
        std::string source_prefix_len;
        std::string next_hop;
        std::string metric;
        std::string ref_count;
        std::string use_count;
        std::string flags;
        std::string iface;
        if (!(stream >> destination >> dest_prefix_len >> source >> source_prefix_len >> next_hop >>
              metric >> ref_count >> use_count >> flags >> iface)) {
            continue;
        }
        if (iface == interface_name && destination == std::string(32, '0') && dest_prefix_len == "00000000") {
            return true;
        }
    }

    return false;
}

bool can_resolve_host() {
    addrinfo hints = {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    addrinfo* result = nullptr;
    const int rc = getaddrinfo("dns.msftncsi.com", nullptr, &hints, &result);
    if (rc != 0) return false;
    freeaddrinfo(result);
    return true;
}

void collect_addresses(rewrite_network_info_t& info) {
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return;

    std::set<std::string> unique_addresses;
    for (ifaddrs* current = interfaces; current != nullptr; current = current->ifa_next) {
        if (!current->ifa_name || info.interface_name != current->ifa_name || !current->ifa_addr) continue;

        char host[NI_MAXHOST] = {};
        const int family = current->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;

        const socklen_t len = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
        if (getnameinfo(current->ifa_addr, len, host, sizeof(host), nullptr, 0, NI_NUMERICHOST) != 0) continue;

        unique_addresses.insert(host);
        if (family == AF_INET) info.has_ipv4_address = true;
        if (family == AF_INET6) info.has_ipv6_address = true;
    }

    freeifaddrs(interfaces);
    info.addresses.assign(unique_addresses.begin(), unique_addresses.end());
}

} // namespace

rewrite_network_info_t rewrite_probe_network() {
    rewrite_network_info_t info;
    info.interface_name = env_or_default("SKEETS_REWRITE_INTERFACE", "eth0");

    const std::string interface_root = "/sys/class/net/" + info.interface_name;
    info.radio_present = path_exists(interface_root);
    if (info.radio_present) {
        info.carrier_up = read_int_flag(interface_root + "/carrier");
        const auto operstate = read_first_line(interface_root + "/operstate");
        info.operstate_up = operstate && *operstate == "up";
        collect_addresses(info);
        info.has_default_ipv4_route = has_default_ipv4_route_for_interface(info.interface_name);
        info.has_default_ipv6_route = has_default_ipv6_route_for_interface(info.interface_name);
        info.can_resolve_dns = can_resolve_host();
    }

    const bool has_address = info.has_ipv4_address || info.has_ipv6_address;
    const bool has_default_route = info.has_default_ipv4_route || info.has_default_ipv6_route;
    info.online = info.radio_present && info.operstate_up && has_address && has_default_route && info.can_resolve_dns;
    return info;
}