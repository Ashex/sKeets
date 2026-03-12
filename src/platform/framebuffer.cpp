#include "platform/framebuffer.h"

#include <fbink.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace {

bool env_flag_enabled(const char* key) {
    const char* value = std::getenv(key);
    return value && std::strcmp(value, "1") == 0;
}

void set_error(std::string* error_message, const char* message) {
    if (error_message) *error_message = message;
}

void set_errno_error(std::string* error_message, const char* prefix) {
    if (!error_message) return;
    *error_message = std::string(prefix) + ": " + std::strerror(errno);
}

skeets_rect_t clamp_rect(const skeets_framebuffer_t& framebuffer, const skeets_rect_t& rect) {
    skeets_rect_t clamped = rect;
    if (clamped.x < 0) {
        clamped.width += clamped.x;
        clamped.x = 0;
    }
    if (clamped.y < 0) {
        clamped.height += clamped.y;
        clamped.y = 0;
    }
    if (clamped.x + clamped.width > framebuffer.info.screen_width) {
        clamped.width = framebuffer.info.screen_width - clamped.x;
    }
    if (clamped.y + clamped.height > framebuffer.info.screen_height) {
        clamped.height = framebuffer.info.screen_height - clamped.y;
    }
    if (clamped.width < 0) clamped.width = 0;
    if (clamped.height < 0) clamped.height = 0;
    return clamped;
}

bool rect_is_empty(const skeets_rect_t& rect) {
    return rect.width <= 0 || rect.height <= 0;
}

bool refresh_region(skeets_framebuffer_t& framebuffer,
                    const skeets_rect_t* rect,
                    skeets_refresh_mode_t mode,
                    bool wait_for_complete,
                    std::string* error_message) {
    if (framebuffer.fd < 0) {
        set_error(error_message, "framebuffer is not open");
        return false;
    }

    FBInkConfig config = {};
    std::uint32_t top = 0;
    std::uint32_t left = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    switch (mode) {
    case skeets_refresh_mode_t::full:
        config.is_flashing = true;
        break;
    case skeets_refresh_mode_t::partial:
        config.wfm_mode = WFM_DU;
        break;
    case skeets_refresh_mode_t::fast:
        config.wfm_mode = WFM_A2;
        break;
    case skeets_refresh_mode_t::grayscale_partial:
        config.wfm_mode = WFM_GC16;
        break;
    }

    if (mode != skeets_refresh_mode_t::full) {
        if (!rect) {
            set_error(error_message, "refresh rectangle is required");
            return false;
        }

        const skeets_rect_t clamped = clamp_rect(framebuffer, *rect);
        if (rect_is_empty(clamped)) {
            set_error(error_message, "refresh rectangle is empty after clipping");
            return false;
        }

        top = static_cast<std::uint32_t>(clamped.y);
        left = static_cast<std::uint32_t>(clamped.x);
        width = static_cast<std::uint32_t>(clamped.width);
        height = static_cast<std::uint32_t>(clamped.height);
    }

    errno = 0;
    if (fbink_refresh(framebuffer.fd, top, left, width, height, &config) < 0) {
        set_errno_error(error_message, "fbink_refresh");
        return false;
    }

    if (wait_for_complete && !skeets_framebuffer_wait_for_complete(framebuffer, error_message)) {
        return false;
    }

    return true;
}

} // namespace

bool skeets_framebuffer_open(skeets_framebuffer_t& framebuffer, std::string* error_message) {
    framebuffer = {};
    framebuffer.fd = -1;

    errno = 0;
    framebuffer.fd = fbink_open();
    if (framebuffer.fd < 0) {
        set_errno_error(error_message, "fbink_open");
        return false;
    }

    FBInkConfig config = {};
    config.is_quiet = true;
    if (fbink_init(framebuffer.fd, &config) < 0) {
        set_errno_error(error_message, "fbink_init");
        fbink_close(framebuffer.fd);
        framebuffer.fd = -1;
        return false;
    }

    FBInkState state = {};
    fbink_get_state(&config, &state);

    framebuffer.info.screen_width = static_cast<int>(state.screen_width);
    framebuffer.info.screen_height = static_cast<int>(state.screen_height);
    framebuffer.info.view_width = static_cast<int>(state.view_width);
    framebuffer.info.view_height = static_cast<int>(state.view_height);
    framebuffer.info.font_width = state.font_w > 0 ? static_cast<int>(state.font_w) : 8;
    framebuffer.info.font_height = state.font_h > 0 ? static_cast<int>(state.font_h) : 16;
    framebuffer.info.is_color_panel = env_flag_enabled("SKEETS_IS_COLOR");
    framebuffer.info.is_mtk_panel = env_flag_enabled("SKEETS_IS_MTK");
    return true;
}

void skeets_framebuffer_close(skeets_framebuffer_t& framebuffer) {
    fbink_free_ot_fonts();
    if (framebuffer.fd >= 0) {
        fbink_close(framebuffer.fd);
    }
    framebuffer = {};
    framebuffer.fd = -1;
}

void skeets_framebuffer_clear(skeets_framebuffer_t& framebuffer, std::uint8_t grayscale) {
    if (framebuffer.fd < 0) return;

    FBInkConfig config = {};
    config.no_refresh = true;
    config.bg_color = grayscale;
    fbink_cls(framebuffer.fd, &config, nullptr, false);
}

void skeets_framebuffer_fill_rect(skeets_framebuffer_t& framebuffer, const skeets_rect_t& rect, std::uint8_t grayscale) {
    if (framebuffer.fd < 0) return;

    const skeets_rect_t clamped = clamp_rect(framebuffer, rect);
    if (rect_is_empty(clamped)) return;

    FBInkConfig config = {};
    config.no_refresh = true;

    FBInkRect fill_rect = {};
    fill_rect.left = static_cast<unsigned short>(clamped.x);
    fill_rect.top = static_cast<unsigned short>(clamped.y);
    fill_rect.width = static_cast<unsigned short>(clamped.width);
    fill_rect.height = static_cast<unsigned short>(clamped.height);
    fbink_fill_rect_gray(framebuffer.fd, &config, &fill_rect, false, grayscale);
}

void skeets_framebuffer_mark_dirty(skeets_framebuffer_t& framebuffer, const skeets_rect_t& rect) {
    const skeets_rect_t clamped = clamp_rect(framebuffer, rect);
    if (rect_is_empty(clamped)) return;

    if (!framebuffer.has_dirty_region) {
        framebuffer.dirty_region = clamped;
        framebuffer.has_dirty_region = true;
        return;
    }

    const int left = std::min(framebuffer.dirty_region.x, clamped.x);
    const int top = std::min(framebuffer.dirty_region.y, clamped.y);
    const int right = std::max(framebuffer.dirty_region.x + framebuffer.dirty_region.width,
                               clamped.x + clamped.width);
    const int bottom = std::max(framebuffer.dirty_region.y + framebuffer.dirty_region.height,
                                clamped.y + clamped.height);

    framebuffer.dirty_region.x = left;
    framebuffer.dirty_region.y = top;
    framebuffer.dirty_region.width = right - left;
    framebuffer.dirty_region.height = bottom - top;
}

bool skeets_framebuffer_refresh(skeets_framebuffer_t& framebuffer,
                                skeets_refresh_mode_t mode,
                                const skeets_rect_t& rect,
                                bool wait_for_complete,
                                std::string* error_message) {
    return refresh_region(framebuffer,
                          mode == skeets_refresh_mode_t::full ? nullptr : &rect,
                          mode,
                          wait_for_complete,
                          error_message);
}

bool skeets_framebuffer_flush(skeets_framebuffer_t& framebuffer,
                              skeets_refresh_mode_t mode,
                              bool wait_for_complete,
                              std::string* error_message) {
    if (!framebuffer.has_dirty_region) {
        return true;
    }

    const skeets_rect_t dirty_region = framebuffer.dirty_region;
    framebuffer.has_dirty_region = false;
    framebuffer.dirty_region = {};
    return refresh_region(framebuffer, &dirty_region, mode, wait_for_complete, error_message);
}

bool skeets_framebuffer_wait_for_complete(skeets_framebuffer_t& framebuffer, std::string* error_message) {
    if (framebuffer.fd < 0) {
        set_error(error_message, "framebuffer is not open");
        return false;
    }

    errno = 0;
    if (fbink_wait_for_complete(framebuffer.fd, LAST_MARKER) < 0) {
        set_errno_error(error_message, "fbink_wait_for_complete");
        return false;
    }
    return true;
}