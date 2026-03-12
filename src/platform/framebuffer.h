#pragma once

#include <cstdint>
#include <string>

struct skeets_rect_t {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class skeets_refresh_mode_t {
    full,
    partial,
    fast,
    grayscale_partial,
};

struct skeets_framebuffer_info_t {
    int screen_width = 0;
    int screen_height = 0;
    int view_width = 0;
    int view_height = 0;
    int font_width = 0;
    int font_height = 0;
    bool supports_full_refresh = true;
    bool supports_partial_refresh = true;
    bool supports_fast_refresh = true;
    bool supports_grayscale_partial_refresh = true;
    bool supports_wait_for_complete = true;
    bool is_color_panel = false;
    bool is_mtk_panel = false;
};

struct skeets_framebuffer_t {
    int fd = -1;
    skeets_framebuffer_info_t info;
    bool has_dirty_region = false;
    skeets_rect_t dirty_region;
};

bool skeets_framebuffer_open(skeets_framebuffer_t& framebuffer, std::string* error_message = nullptr);
void skeets_framebuffer_close(skeets_framebuffer_t& framebuffer);
void skeets_framebuffer_clear(skeets_framebuffer_t& framebuffer, std::uint8_t grayscale);
void skeets_framebuffer_fill_rect(skeets_framebuffer_t& framebuffer, const skeets_rect_t& rect, std::uint8_t grayscale);
void skeets_framebuffer_mark_dirty(skeets_framebuffer_t& framebuffer, const skeets_rect_t& rect);
bool skeets_framebuffer_refresh(skeets_framebuffer_t& framebuffer,
                                skeets_refresh_mode_t mode,
                                const skeets_rect_t& rect,
                                bool wait_for_complete,
                                std::string* error_message = nullptr);
bool skeets_framebuffer_flush(skeets_framebuffer_t& framebuffer,
                              skeets_refresh_mode_t mode,
                              bool wait_for_complete,
                              std::string* error_message = nullptr);
bool skeets_framebuffer_wait_for_complete(skeets_framebuffer_t& framebuffer, std::string* error_message = nullptr);