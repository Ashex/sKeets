#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class skeets_input_protocol_t {
    unknown,
    standard_multitouch,
    snow,
};

struct skeets_input_device_info_t {
    int fd = -1;
    std::string path;
    std::string name;
    bool is_touch_device = false;
    bool is_key_device = false;
    bool has_mt_slot = false;
    bool has_tracking_id = false;
    int x_code = -1;
    int y_code = -1;
    int x_min = 0;
    int x_max = 0;
    int y_min = 0;
    int y_max = 0;
    bool swap_axes = false;
    bool mirror_x = false;
    bool mirror_y = false;
};

enum class skeets_input_event_type_t {
    none,
    touch_down,
    touch_move,
    touch_up,
    key_press,
};

struct skeets_input_event_t {
    skeets_input_event_type_t type = skeets_input_event_type_t::none;
    std::string source_path;
    int slot = -1;
    int tracking_id = -1;
    int x = 0;
    int y = 0;
    int key_code = 0;
};

struct skeets_input_contact_t {
    int slot = -1;
    int tracking_id = -1;
    int raw_x = 0;
    int raw_y = 0;
    int mapped_x = 0;
    int mapped_y = 0;
    bool active = false;
    bool dirty = false;
    bool pending_down = false;
    bool pending_up = false;
};

struct skeets_input_t {
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    skeets_input_protocol_t protocol = skeets_input_protocol_t::unknown;
    bool debug_raw_events = false;
    int raw_event_log_budget = 0;
    std::vector<skeets_input_device_info_t> devices;
    int touch_device_index = -1;
    int key_device_index = -1;
    std::vector<skeets_input_contact_t> contacts;
    int current_slot = 0;
    bool pending_snow_lift = false;
};

bool skeets_input_open(skeets_input_t& input,
                        int framebuffer_width,
                        int framebuffer_height,
                        skeets_input_protocol_t protocol,
                        std::string* error_message = nullptr);
void skeets_input_close(skeets_input_t& input);
bool skeets_input_poll(skeets_input_t& input,
                        skeets_input_event_t& event,
                        int timeout_ms,
                        std::string* error_message = nullptr);
const char* skeets_input_protocol_name(skeets_input_protocol_t protocol);