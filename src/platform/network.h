#pragma once

#include <string>
#include <vector>

struct skeets_network_info_t {
    std::string interface_name;
    bool radio_present = false;
    bool carrier_up = false;
    bool operstate_up = false;
    bool has_ipv4_address = false;
    bool has_ipv6_address = false;
    std::vector<std::string> addresses;
    bool has_default_ipv4_route = false;
    bool has_default_ipv6_route = false;
    bool can_resolve_dns = false;
    bool online = false;
};

skeets_network_info_t skeets_probe_network();