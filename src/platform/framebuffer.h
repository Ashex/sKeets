#pragma once

#include <cstdint>
#include <string>

struct rewrite_rect_t {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class rewrite_refresh_mode_t {
    full,
    partial,
    fast,
    grayscale_partial,
};

struct rewrite_framebuffer_info_t {
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

struct rewrite_framebuffer_t {
    int fd = -1;
    rewrite_framebuffer_info_t info;
    bool has_dirty_region = false;
    rewrite_rect_t dirty_region;
};

bool rewrite_framebuffer_open(rewrite_framebuffer_t& framebuffer, std::string* error_message = nullptr);
void rewrite_framebuffer_close(rewrite_framebuffer_t& framebuffer);
void rewrite_framebuffer_clear(rewrite_framebuffer_t& framebuffer, std::uint8_t grayscale);
void rewrite_framebuffer_fill_rect(rewrite_framebuffer_t& framebuffer, const rewrite_rect_t& rect, std::uint8_t grayscale);
void rewrite_framebuffer_mark_dirty(rewrite_framebuffer_t& framebuffer, const rewrite_rect_t& rect);
bool rewrite_framebuffer_refresh(rewrite_framebuffer_t& framebuffer,
                                rewrite_refresh_mode_t mode,
                                const rewrite_rect_t& rect,
                                bool wait_for_complete,
                                std::string* error_message = nullptr);
bool rewrite_framebuffer_flush(rewrite_framebuffer_t& framebuffer,
                              rewrite_refresh_mode_t mode,
                              bool wait_for_complete,
                              std::string* error_message = nullptr);
bool rewrite_framebuffer_wait_for_complete(rewrite_framebuffer_t& framebuffer, std::string* error_message = nullptr);