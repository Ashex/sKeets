#include "rewrite/platform/input.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr int kMaxContacts = 16;

bool is_touch_tool_key(int code) {
    return code == BTN_TOUCH ||
           code == BTN_TOOL_FINGER ||
           code == BTN_TOOL_DOUBLETAP ||
           code == BTN_TOOL_TRIPLETAP ||
           code == BTN_TOOL_QUADTAP ||
           code == BTN_TOOL_QUINTTAP;
}

const char* event_type_name(__u16 type) {
    switch (type) {
    case EV_SYN: return "EV_SYN";
    case EV_KEY: return "EV_KEY";
    case EV_ABS: return "EV_ABS";
    default: return "EV_OTHER";
    }
}

void log_raw_event(rewrite_input_t& input, const rewrite_input_device_info_t& device, const input_event& ev) {
    if (!input.debug_raw_events || input.raw_event_log_budget <= 0) return;
    --input.raw_event_log_budget;
    std::fprintf(stderr,
                 "rewrite diag: input.raw src=%s type=%s code=%u value=%d\n",
                 device.path.c_str(),
                 event_type_name(ev.type),
                 static_cast<unsigned int>(ev.code),
                 ev.value);
}

void set_error(std::string* error_message, const std::string& message) {
    if (error_message) *error_message = message;
}

template <size_t N>
bool test_bit(const std::array<unsigned long, N>& bits, int bit) {
    const size_t index = static_cast<size_t>(bit) / (sizeof(unsigned long) * CHAR_BIT);
    const size_t offset = static_cast<size_t>(bit) % (sizeof(unsigned long) * CHAR_BIT);
    if (index >= bits.size()) return false;
    return (bits[index] & (1UL << offset)) != 0;
}

bool query_abs_axis(int fd, int primary_code, int fallback_code,
                    int& resolved_code, int& min_value, int& max_value) {
    input_absinfo absinfo = {};
    if (ioctl(fd, EVIOCGABS(primary_code), &absinfo) == 0) {
        resolved_code = primary_code;
        min_value = absinfo.minimum;
        max_value = absinfo.maximum;
        return true;
    }

    if (ioctl(fd, EVIOCGABS(fallback_code), &absinfo) == 0) {
        resolved_code = fallback_code;
        min_value = absinfo.minimum;
        max_value = absinfo.maximum;
        return true;
    }

    return false;
}

bool should_swap_axes(const rewrite_input_device_info_t& device, int fb_w, int fb_h) {
    const int x_span = device.x_max - device.x_min;
    const int y_span = device.y_max - device.y_min;
    const int normal_score = std::abs(x_span - (fb_w - 1)) + std::abs(y_span - (fb_h - 1));
    const int swapped_score = std::abs(x_span - (fb_h - 1)) + std::abs(y_span - (fb_w - 1));
    return swapped_score < normal_score;
}

int normalize_axis(int value, int min_value, int max_value, int output_max) {
    if (output_max <= 0) return 0;
    if (max_value <= min_value) {
        return std::clamp(value, 0, output_max);
    }

    value = std::clamp(value, min_value, max_value);
    const long long numerator = static_cast<long long>(value - min_value) * output_max;
    const long long denominator = static_cast<long long>(max_value - min_value);
    return static_cast<int>(numerator / denominator);
}

void map_touch_point(const rewrite_input_t& input,
                     const rewrite_input_device_info_t& device,
                     int raw_x,
                     int raw_y,
                     int& mapped_x,
                     int& mapped_y) {
    int x = normalize_axis(raw_x, device.x_min, device.x_max,
                           device.swap_axes ? input.framebuffer_height - 1 : input.framebuffer_width - 1);
    int y = normalize_axis(raw_y, device.y_min, device.y_max,
                           device.swap_axes ? input.framebuffer_width - 1 : input.framebuffer_height - 1);
    if (device.swap_axes) std::swap(x, y);
    mapped_x = x;
    mapped_y = y;
}

rewrite_input_contact_t& ensure_contact(rewrite_input_t& input, int slot) {
    if (slot < 0) slot = 0;
    if (slot >= static_cast<int>(input.contacts.size())) {
        input.contacts.resize(std::max(slot + 1, kMaxContacts));
    }
    rewrite_input_contact_t& contact = input.contacts[slot];
    if (contact.slot < 0) contact.slot = slot;
    return contact;
}

bool enqueue_contact_event(const rewrite_input_t& input,
                           const rewrite_input_device_info_t& device,
                           rewrite_input_contact_t& contact,
                           rewrite_input_event_t& event) {
    if (contact.pending_up) {
        event.type = rewrite_input_event_type_t::touch_up;
        event.source_path = device.path;
        event.slot = contact.slot;
        event.tracking_id = contact.tracking_id;
        event.x = contact.mapped_x;
        event.y = contact.mapped_y;
        contact.pending_up = false;
        contact.active = false;
        contact.dirty = false;
        contact.tracking_id = -1;
        return true;
    }

    if (contact.pending_down) {
        event.type = rewrite_input_event_type_t::touch_down;
        event.source_path = device.path;
        event.slot = contact.slot;
        event.tracking_id = contact.tracking_id;
        event.x = contact.mapped_x;
        event.y = contact.mapped_y;
        contact.pending_down = false;
        contact.dirty = false;
        return true;
    }

    if (contact.active && contact.dirty) {
        event.type = rewrite_input_event_type_t::touch_move;
        event.source_path = device.path;
        event.slot = contact.slot;
        event.tracking_id = contact.tracking_id;
        event.x = contact.mapped_x;
        event.y = contact.mapped_y;
        contact.dirty = false;
        return true;
    }

    return false;
}

bool flush_contact_events(rewrite_input_t& input, rewrite_input_event_t& event) {
    if (input.touch_device_index < 0 || input.touch_device_index >= static_cast<int>(input.devices.size())) {
        return false;
    }

    const rewrite_input_device_info_t& device = input.devices[input.touch_device_index];
    for (rewrite_input_contact_t& contact : input.contacts) {
        if (contact.slot < 0) continue;
        if (enqueue_contact_event(input, device, contact, event)) return true;
    }
    return false;
}

std::string device_name_for_fd(int fd) {
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && name[0] != '\0') {
        return name;
    }
    return "unknown";
}

bool scan_input_device(const std::string& path,
                       int fd,
                       int fb_w,
                       int fb_h,
                       rewrite_input_device_info_t& info) {
    info = {};
    info.fd = fd;
    info.path = path;
    info.name = device_name_for_fd(fd);

    std::array<unsigned long, (EV_MAX / (sizeof(unsigned long) * CHAR_BIT)) + 1> ev_bits{};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits.data()) < 0) {
        return false;
    }

    const bool has_abs = test_bit(ev_bits, EV_ABS);
    const bool has_key = test_bit(ev_bits, EV_KEY);

    if (has_abs) {
        input_absinfo absinfo = {};
        info.is_touch_device = query_abs_axis(fd, ABS_MT_POSITION_X, ABS_X, info.x_code, info.x_min, info.x_max) &&
                               query_abs_axis(fd, ABS_MT_POSITION_Y, ABS_Y, info.y_code, info.y_min, info.y_max);
        info.has_mt_slot = ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &absinfo) == 0;
        info.has_tracking_id = ioctl(fd, EVIOCGABS(ABS_MT_TRACKING_ID), &absinfo) == 0;
        if (info.is_touch_device) {
            info.swap_axes = should_swap_axes(info, fb_w, fb_h);
        }
    }

    if (has_key) {
        std::array<unsigned long, (KEY_MAX / (sizeof(unsigned long) * CHAR_BIT)) + 1> key_bits{};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data()) >= 0) {
            info.is_key_device = test_bit(key_bits, BTN_TOUCH) || test_bit(key_bits, KEY_POWER) ||
                                 test_bit(key_bits, KEY_NEXTSONG) || test_bit(key_bits, KEY_PREVIOUSSONG);
        }
    }

    return info.is_touch_device || info.is_key_device;
}

void handle_touch_abs(rewrite_input_t& input, const rewrite_input_device_info_t& device, const input_event& ev) {
    if (input.protocol == rewrite_input_protocol_t::snow) {
        if (ev.code == ABS_MT_SLOT) {
            input.current_slot = ev.value;
            ensure_contact(input, input.current_slot);
            return;
        }
        if (ev.code == ABS_MT_TRACKING_ID) {
            if (ev.value == -1) {
                input.protocol = rewrite_input_protocol_t::standard_multitouch;
                rewrite_input_contact_t& contact = ensure_contact(input, input.current_slot);
                if (contact.active) contact.pending_up = true;
                return;
            }

            input.current_slot = ev.value;
            rewrite_input_contact_t& contact = ensure_contact(input, input.current_slot);
            contact.tracking_id = ev.value;
            contact.active = true;
            contact.pending_down = true;
            return;
        }
    } else if (ev.code == ABS_MT_SLOT) {
        input.current_slot = ev.value;
        ensure_contact(input, input.current_slot);
        return;
    } else if (ev.code == ABS_MT_TRACKING_ID) {
        rewrite_input_contact_t& contact = ensure_contact(input, input.current_slot);
        if (ev.value == -1) {
            if (contact.active) contact.pending_up = true;
            return;
        }

        contact.tracking_id = ev.value;
        contact.active = true;
        contact.pending_down = true;
        return;
    }

    rewrite_input_contact_t& contact = ensure_contact(input, input.current_slot);
    if (ev.code == device.x_code) {
        contact.raw_x = ev.value;
        map_touch_point(input, device, contact.raw_x, contact.raw_y, contact.mapped_x, contact.mapped_y);
        if (!contact.pending_down) contact.dirty = true;
    } else if (ev.code == device.y_code) {
        contact.raw_y = ev.value;
        map_touch_point(input, device, contact.raw_x, contact.raw_y, contact.mapped_x, contact.mapped_y);
        if (!contact.pending_down) contact.dirty = true;
    }
}

void handle_touch_key(rewrite_input_t& input, const input_event& ev, rewrite_input_event_t& event) {
    if (ev.code == BTN_TOUCH && ev.value == 0 && input.protocol == rewrite_input_protocol_t::snow) {
        input.pending_snow_lift = true;
        return;
    }

    if (is_touch_tool_key(ev.code)) {
        return;
    }

    if (ev.value == 1) {
        event.type = rewrite_input_event_type_t::key_press;
        event.key_code = ev.code;
    }
}

void finalize_snow_frame(rewrite_input_t& input) {
    if (!input.pending_snow_lift) return;
    input.pending_snow_lift = false;
    for (rewrite_input_contact_t& contact : input.contacts) {
        if (contact.slot >= 0 && contact.active) {
            contact.pending_up = true;
        }
    }
}

} // namespace

const char* rewrite_input_protocol_name(rewrite_input_protocol_t protocol) {
    switch (protocol) {
    case rewrite_input_protocol_t::standard_multitouch:
        return "standard_multitouch";
    case rewrite_input_protocol_t::snow:
        return "snow";
    case rewrite_input_protocol_t::unknown:
    default:
        return "unknown";
    }
}

bool rewrite_input_open(rewrite_input_t& input,
                        int framebuffer_width,
                        int framebuffer_height,
                        rewrite_input_protocol_t protocol,
                        std::string* error_message) {
    input = {};
    input.framebuffer_width = framebuffer_width;
    input.framebuffer_height = framebuffer_height;
    input.protocol = protocol;
    input.contacts.resize(kMaxContacts);
    for (int index = 0; index < kMaxContacts; ++index) input.contacts[index].slot = index;

    DIR* dir = opendir("/dev/input");
    if (!dir) {
        set_error(error_message, std::string("opendir(/dev/input): ") + std::strerror(errno));
        return false;
    }

    while (dirent* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        rewrite_input_device_info_t info;
        if (!scan_input_device(path, fd, framebuffer_width, framebuffer_height, info)) {
            close(fd);
            continue;
        }

        if (ioctl(fd, EVIOCGRAB, 1) != 0) {
            close(fd);
            continue;
        }

        const int index = static_cast<int>(input.devices.size());
        input.devices.push_back(std::move(info));
        if (input.devices[index].is_touch_device && input.touch_device_index < 0) input.touch_device_index = index;
        if (input.devices[index].is_key_device && input.key_device_index < 0) input.key_device_index = index;
    }
    closedir(dir);

    if (input.devices.empty()) {
        set_error(error_message, "no matching input devices found under /dev/input");
        return false;
    }

    if (input.touch_device_index < 0) {
        set_error(error_message, "no touch-capable input device found");
        return false;
    }

    return true;
}

void rewrite_input_close(rewrite_input_t& input) {
    for (rewrite_input_device_info_t& device : input.devices) {
        if (device.fd >= 0) {
            ioctl(device.fd, EVIOCGRAB, 0);
            close(device.fd);
            device.fd = -1;
        }
    }
    input = {};
}

bool rewrite_input_poll(rewrite_input_t& input,
                        rewrite_input_event_t& event,
                        int timeout_ms,
                        std::string* error_message) {
    event = {};
    if (flush_contact_events(input, event)) return true;

    if (input.devices.empty()) {
        set_error(error_message, "input module is not open");
        return false;
    }

    std::vector<pollfd> poll_fds;
    poll_fds.reserve(input.devices.size());
    std::vector<int> device_indexes;
    device_indexes.reserve(input.devices.size());
    for (size_t index = 0; index < input.devices.size(); ++index) {
        const rewrite_input_device_info_t& device = input.devices[index];
        if (device.fd < 0) continue;
        poll_fds.push_back({device.fd, POLLIN, 0});
        device_indexes.push_back(static_cast<int>(index));
    }

    if (poll_fds.empty()) {
        set_error(error_message, "no input fds available for polling");
        return false;
    }

    const int poll_result = poll(poll_fds.data(), poll_fds.size(), timeout_ms);
    if (poll_result < 0) {
        set_error(error_message, std::string("poll(input): ") + std::strerror(errno));
        return false;
    }
    if (poll_result == 0) {
        return false;
    }

    for (size_t index = 0; index < poll_fds.size(); ++index) {
        if ((poll_fds[index].revents & POLLIN) == 0) continue;

        const rewrite_input_device_info_t& device = input.devices[device_indexes[index]];
        input_event raw_event = {};
        while (read(poll_fds[index].fd, &raw_event, sizeof(raw_event)) == sizeof(raw_event)) {
            log_raw_event(input, device, raw_event);
            if (device.is_touch_device && raw_event.type == EV_ABS) {
                handle_touch_abs(input, device, raw_event);
            } else if (raw_event.type == EV_KEY) {
                handle_touch_key(input, raw_event, event);
                if (event.type == rewrite_input_event_type_t::key_press &&
                    device_indexes[index] == input.key_device_index) {
                    event.source_path = device.path;
                    return true;
                }
            } else if (device.is_touch_device && raw_event.type == EV_SYN && raw_event.code == SYN_REPORT) {
                finalize_snow_frame(input);
                if (flush_contact_events(input, event)) {
                    return true;
                }
            }
        }
    }
    return false;
}