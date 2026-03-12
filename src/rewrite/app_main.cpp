#include "rewrite/bootstrap.h"
#include "rewrite/actions.h"
#include "rewrite/feed.h"
#include "rewrite/thread.h"
#include "rewrite/platform/device.h"
#include "rewrite/platform/framebuffer.h"
#include "rewrite/platform/input.h"
#include "rewrite/platform/network.h"
#include "rewrite/platform/power.h"
#include "util/config.h"
#include "util/image.h"
#include "util/image_cache.h"
#include "util/paths.h"
#include "ui/fb.h"
#include "ui/font.h"
#include "util/str.h"

#include <QCoreApplication>
#include <QFile>
#include <QSslCertificate>
#include <QSslConfiguration>

#include <linux/input.h>

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

constexpr std::uint8_t kColorHeader = 0x18;
constexpr std::uint8_t kColorCard = 0xEC;
constexpr std::uint8_t kColorCardBorder = 0x30;
constexpr std::uint8_t kColorCardTitle = 0x28;
constexpr std::uint8_t kColorButtonPrimary = 0x20;
constexpr std::uint8_t kColorButtonSecondary = 0x90;
constexpr std::uint8_t kColorStatus = 0xE2;
constexpr int kOuterMargin = 28;
constexpr int kCardGap = 20;
constexpr int kCardPadding = 18;
constexpr int kPostSeparator = 12;
constexpr int kFeedAvatarSize = 100;
constexpr int kThreadAvatarSize = 90;
constexpr int kAvatarGap = 12;
constexpr int kFeedEmbedMaxHeight = 168;
constexpr int kQuoteIndent = 24;
constexpr int kQuoteBorder = 3;
constexpr int kExternalThumbSize = 72;
constexpr int kThreadScrollbarWidth = 18;
constexpr int kThreadScrollbarGap = 12;
constexpr int kEmbedImageGap = 6;
constexpr int kHeaderLogoMaxWidth = 220;
constexpr int kHeaderLogoMaxHeight = 48;
constexpr int kSplashLogoMaxWidth = 520;
constexpr int kSplashLogoMaxHeight = 190;
constexpr std::uint8_t kColorPostBorder = 0xC0;
constexpr std::uint8_t kColorAuthor = 0x10;
constexpr std::uint8_t kColorMeta = 0x60;
constexpr std::uint8_t kColorRepost = 0x50;

enum class rewrite_view_mode_t {
    dashboard,
    feed,
    thread,
    settings,
    diagnostics,
};

struct rewrite_post_hit_t {
    rewrite_rect_t rect;
    rewrite_rect_t like_rect;
    rewrite_rect_t repost_rect;
    rewrite_rect_t stats_rect;
    int stats_x = 0;
    int stats_y = 0;
    int post_index = -1;
};

struct rewrite_thread_post_hit_t {
    rewrite_rect_t rect;
    rewrite_rect_t like_rect;
    rewrite_rect_t repost_rect;
    rewrite_rect_t stats_rect;
    int stats_x = 0;
    int stats_y = 0;
    Bsky::Post* post = nullptr;
};

struct rewrite_button_t {
    rewrite_rect_t rect;
    std::string label;
};

enum class rewrite_text_role_t {
    body,
    author,
    meta,
    button,
    card_title,
    status_title,
    status_detail,
    settings_label,
    settings_detail,
    action_label,
    splash_title,
    splash_detail,
    fallback_brand,
};

struct rewrite_text_style_t {
    int size_px;
    font_style_t style;
    int line_spacing;
};

struct rewrite_app_t {
    rewrite_framebuffer_t framebuffer;
    rewrite_input_t input;
    fb_t text_fb = {};
    rewrite_bootstrap_result_t bootstrap;
    rewrite_device_info_t device;
    rewrite_power_info_t power;
    rewrite_network_info_t network;
    image_t header_logo{};
    image_t splash_logo{};
    std::string revision_summary;
    std::string status_line = "Checking rewrite auth bootstrap";
    std::string input_line = "Exit via the on-screen button or the hardware power key";
    rewrite_button_t refresh_button;
    rewrite_button_t exit_button;
    rewrite_button_t diagnostics_back_button;
    rewrite_button_t diagnostics_refresh_button;

    // Feed view state
    rewrite_view_mode_t view_mode = rewrite_view_mode_t::dashboard;
    rewrite_feed_result_t feed_result;
    int feed_page_start = 0;      // Index of first visible post
    int feed_page_count = 0;      // Number of posts that fit on last render
    std::vector<int> feed_page_history;
    std::vector<rewrite_post_hit_t> visible_post_hits;
    rewrite_button_t feed_back_button;
    rewrite_button_t feed_refresh_button;
    rewrite_button_t feed_next_button;
    rewrite_button_t feed_settings_button;

    // Thread view state
    rewrite_thread_result_t thread_result;
    std::string selected_post_uri;
    int thread_page_start = 0;
    int thread_page_count = 0;
    std::vector<rewrite_thread_post_hit_t> thread_post_hits;
    rewrite_button_t thread_back_button;
    rewrite_button_t thread_settings_button;
    rewrite_rect_t thread_scrollbar_rect{};
    rewrite_rect_t thread_scrollbar_thumb_rect{};

    // Settings view state
    bool profile_images_enabled = true;
    bool embed_images_enabled = false;
    bool allow_nudity_content = false;
    bool allow_porn_content = false;
    bool allow_suggestive_content = false;
    rewrite_view_mode_t settings_return_view = rewrite_view_mode_t::feed;
    rewrite_button_t settings_back_button;
    rewrite_button_t settings_profile_button;
    rewrite_button_t settings_embed_button;
    rewrite_button_t settings_nudity_button;
    rewrite_button_t settings_porn_button;
    rewrite_button_t settings_suggestive_button;
    rewrite_button_t settings_diagnostics_button;
    rewrite_button_t settings_sign_out_button;
};

void draw_border(rewrite_app_t& app, const rewrite_rect_t& rect, std::uint8_t color, int thickness = 2);
void flatten_thread_posts(const Bsky::Post& post,
                          int depth,
                          std::vector<std::pair<const Bsky::Post*, int>>& out);

void handle_signal(int) {
    g_stop_requested = 1;
}

std::string bool_label(bool value) {
    return value ? "yes" : "no";
}

rewrite_text_style_t text_style(rewrite_text_role_t role) {
    switch (role) {
    case rewrite_text_role_t::author:
        return {16, FONT_STYLE_MEDIUM, 4};
    case rewrite_text_role_t::meta:
        return {14, FONT_STYLE_LIGHT, 4};
    case rewrite_text_role_t::button:
        return {16, FONT_STYLE_MEDIUM, 0};
    case rewrite_text_role_t::card_title:
        return {16, FONT_STYLE_MEDIUM, 4};
    case rewrite_text_role_t::status_title:
        return {17, FONT_STYLE_MEDIUM, 4};
    case rewrite_text_role_t::status_detail:
        return {15, FONT_STYLE_LIGHT, 4};
    case rewrite_text_role_t::settings_label:
        return {16, FONT_STYLE_MEDIUM, 4};
    case rewrite_text_role_t::settings_detail:
        return {14, FONT_STYLE_LIGHT, 4};
    case rewrite_text_role_t::action_label:
        return {14, FONT_STYLE_MEDIUM, 0};
    case rewrite_text_role_t::splash_title:
        return {28, FONT_STYLE_MEDIUM, 6};
    case rewrite_text_role_t::splash_detail:
        return {18, FONT_STYLE_LIGHT, 6};
    case rewrite_text_role_t::fallback_brand:
        return {24, FONT_STYLE_MEDIUM, 4};
    case rewrite_text_role_t::body:
    default:
        return {14, FONT_STYLE_REGULAR, 4};
    }
}

int measure_text(const std::string& text, rewrite_text_role_t role) {
    const rewrite_text_style_t style = text_style(role);
    return font_measure_string_styled(text.c_str(), style.size_px, style.style);
}

int draw_text(rewrite_app_t& app,
              int x,
              int y,
              const std::string& text,
              rewrite_text_role_t role,
              std::uint8_t fg,
              std::uint8_t bg) {
    const rewrite_text_style_t style = text_style(role);
    return font_draw_string_styled(&app.text_fb, x, y, text.c_str(), fg, bg, style.size_px, style.style);
}

int draw_wrapped_text(rewrite_app_t& app,
                      int x,
                      int y,
                      int width,
                      const std::string& text,
                      rewrite_text_role_t role,
                      std::uint8_t fg,
                      std::uint8_t bg) {
    const rewrite_text_style_t style = text_style(role);
    return font_draw_wrapped_styled(&app.text_fb,
                                    x,
                                    y,
                                    width,
                                    text.c_str(),
                                    fg,
                                    bg,
                                    style.line_spacing,
                                    style.size_px,
                                    style.style);
}

int line_height(rewrite_text_role_t role) {
    const rewrite_text_style_t style = text_style(role);
    return font_line_height(style.size_px, style.style);
}

std::string to_lower_ascii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}

std::string sanitize_bitmap_text(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            continue;
        }

        if (i + 2 < text.size()) {
            const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            if (c == 0xE2 && c1 == 0x80 && c2 == 0x94) {
                out.push_back('-');
                i += 2;
                continue;
            }
            if (c == 0xE2 && c1 == 0x80 && (c2 == 0x98 || c2 == 0x99)) {
                out.push_back('\'');
                i += 2;
                continue;
            }
        }

        out.push_back('?');
        while (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
            ++i;
        }
    }
    return out;
}

int measure_quote_embed_height(const rewrite_app_t& app, int width, const Bsky::Post* quoted) {
    if (!quoted) return 0;
    const int inner_width = std::max(40, width - kQuoteIndent - 8);
    const std::string text = sanitize_bitmap_text(quoted->text);
    const int body_height = font_measure_wrapped(inner_width,
                                                 text.c_str(),
                                                 4);
    return app.text_fb.font_h + 4 + body_height + 8;
}

void draw_quote_embed(rewrite_app_t& app, int x, int y, int width, const Bsky::Post* quoted) {
    if (!quoted) return;
    const int height = measure_quote_embed_height(app, width, quoted);
    rewrite_framebuffer_fill_rect(app.framebuffer,
                                  rewrite_rect_t{x, y, kQuoteBorder, height},
                                  kColorMeta);
    draw_border(app, rewrite_rect_t{x, y, width, height}, kColorPostBorder, 1);

    const int quote_x = x + kQuoteIndent;
    const int quote_width = std::max(40, width - kQuoteIndent - 8);
    int quote_y = y + kCardPadding / 2;
    std::string author = quoted->author.display_name.empty()
        ? "@" + quoted->author.handle
        : quoted->author.display_name + "  @" + quoted->author.handle;
    author = sanitize_bitmap_text(author);
    font_draw_string(&app.text_fb,
                     quote_x,
                     quote_y,
                     author.c_str(),
                     kColorMeta,
                     COLOR_WHITE);
    quote_y += app.text_fb.font_h + 2;
    const std::string text = sanitize_bitmap_text(quoted->text);
    font_draw_wrapped(&app.text_fb,
                      quote_x,
                      quote_y,
                      quote_width,
                      text.c_str(),
                      COLOR_BLACK,
                      COLOR_WHITE,
                      4);
}

int target_preview_thumb_width(const rewrite_app_t& app, int available_width);
int single_image_embed_height(const rewrite_app_t& app, int embed_width);
int image_embed_tile_height(const rewrite_app_t& app, int embed_width, int image_count);

bool preview_text_needs_stacked_layout(int side_text_height,
                                       int thumb_height,
                                       int side_text_width,
                                       bool has_thumb) {
    return has_thumb && (side_text_height > thumb_height || side_text_width < 220);
}

int measure_external_embed_height(const rewrite_app_t& app, int width, const Bsky::Post& post, bool embed_images) {
    const bool has_thumb = embed_images && !post.ext_thumb_url.empty();
    const bool alt_prefixed_description = post.ext_description.rfind("ALT:", 0) == 0;
    const bool title_only = alt_prefixed_description && !post.ext_title.empty();
    const int thumb_width = has_thumb ? target_preview_thumb_width(app, width) : 0;
    const int thumb_height = has_thumb ? std::max(96, (thumb_width * 3) / 4) : 0;
    const int side_text_width = has_thumb ? std::max(120, width - thumb_width - 18) : std::max(120, width - 12);
    const int full_text_width = std::max(120, width - 12);
    const std::string title = sanitize_bitmap_text(post.ext_title.empty() ? "External media" : post.ext_title);
    const std::string description = title_only
        ? std::string{}
        : sanitize_bitmap_text(post.ext_description.empty() ? "External media preview" : post.ext_description);
    const int side_title_height = font_measure_wrapped(side_text_width, title.c_str(), 4);
    const int side_description_height = description.empty() ? 0 : font_measure_wrapped(side_text_width, description.c_str(), 4);
    const int side_text_height = side_title_height + (description.empty() ? 0 : (side_description_height + 4));
    if (preview_text_needs_stacked_layout(side_text_height, thumb_height, side_text_width, has_thumb)) {
        const int full_title_height = font_measure_wrapped(full_text_width, title.c_str(), 4);
        const int full_description_height = description.empty() ? 0 : font_measure_wrapped(full_text_width, description.c_str(), 4);
        const int full_text_height = full_title_height + (description.empty() ? 0 : (full_description_height + 4));
        return thumb_height + 8 + full_text_height + 12;
    }
    return std::max(thumb_height, side_text_height) + 12;
}

std::vector<std::string> image_alt_lines(const Bsky::Post& post) {
    std::vector<std::string> lines;
    const int image_count = static_cast<int>(post.image_urls.size());
    for (int index = 0; index < image_count; ++index) {
        std::string alt;
        if (index < static_cast<int>(post.image_alts.size())) {
            alt = sanitize_bitmap_text(post.image_alts[index]);
        }
        if (alt.empty()) {
            alt = image_count == 1
                ? "Image attachment"
                : "Image " + std::to_string(index + 1) + " attachment";
        } else if (image_count > 1) {
            alt = "Image " + std::to_string(index + 1) + ": " + alt;
        }
        lines.push_back(std::move(alt));
    }
    if (lines.empty()) {
        lines.push_back("Image attachment");
    }
    return lines;
}

int measure_image_alt_height(const rewrite_app_t& app, const Bsky::Post& post, int width) {
    const int text_width = std::max(120, width - 12);
    int total = 8;
    for (const auto& line : image_alt_lines(post)) {
        total += font_measure_wrapped(text_width, line.c_str(), 4) + 6;
    }
    return std::max(total, app.text_fb.font_h + 16);
}

void draw_image_alt_block(rewrite_app_t& app, const Bsky::Post& post, int x, int& y, int width) {
    const int height = measure_image_alt_height(app, post, width);
    draw_border(app, rewrite_rect_t{x, y, width, height}, kColorPostBorder, 1);
    int text_y = y + 6;
    for (const auto& line : image_alt_lines(post)) {
        text_y = font_draw_wrapped(&app.text_fb,
                                   x + 6,
                                   text_y,
                                   std::max(120, width - 12),
                                   line.c_str(),
                                   kColorMeta,
                                   COLOR_WHITE,
                                   4);
        text_y += 6;
    }
    y += height + 8;
}

int target_preview_thumb_width(const rewrite_app_t& app, int available_width) {
    const int inner_width = std::max(120, available_width - 12);
    const int width_target = (inner_width * 2) / 5;
    const int screen_target = app.framebuffer.info.screen_width / 3;
    return std::max(120, std::min(inner_width, std::min(width_target, screen_target)));
}

int measure_unsupported_media_height(const rewrite_app_t& app, const Bsky::Post& post, int width) {
    const bool has_preview = !post.media_preview_url.empty();
    const int thumb_width = has_preview ? target_preview_thumb_width(app, width) : 0;
    const int thumb_height = has_preview ? std::max(96, (thumb_width * 3) / 4) : 0;
    const int side_text_width = has_preview ? std::max(120, width - thumb_width - 18) : std::max(120, width - 12);
    const int full_text_width = std::max(120, width - 12);
    const std::string label = sanitize_bitmap_text(post.media_label.empty() ? "Unsupported media" : post.media_label);
    const std::string alt = sanitize_bitmap_text(post.media_alt_text.empty() ? "Preview unavailable for this media type." : post.media_alt_text);
    const int side_label_height = font_measure_wrapped(side_text_width, label.c_str(), 4);
    const int side_alt_height = font_measure_wrapped(side_text_width, alt.c_str(), 4);
    const int side_text_height = side_label_height + 4 + side_alt_height;
    if (preview_text_needs_stacked_layout(side_text_height, thumb_height, side_text_width, has_preview)) {
        const int full_label_height = font_measure_wrapped(full_text_width, label.c_str(), 4);
        const int full_alt_height = font_measure_wrapped(full_text_width, alt.c_str(), 4);
        return thumb_height + 8 + full_label_height + 4 + full_alt_height + 12;
    }
    return std::max(thumb_height, side_text_height) + 12;
}

void draw_unsupported_media_block(rewrite_app_t& app, const Bsky::Post& post, int x, int& y, int width) {
    const int height = measure_unsupported_media_height(app, post, width);
    draw_border(app, rewrite_rect_t{x, y, width, height}, kColorPostBorder, 1);

    const bool has_preview = !post.media_preview_url.empty();
    const int thumb_width = has_preview ? target_preview_thumb_width(app, width) : 0;
    const int thumb_height = has_preview ? std::max(96, (thumb_width * 3) / 4) : 0;
    const int thumb_x = x + 6;
    const int thumb_y = y + 6;
    int text_x = x + 6;
    int text_width = std::max(120, width - 12);
    int text_y = y + 6;
    const std::string label = sanitize_bitmap_text(post.media_label.empty() ? "Unsupported media" : post.media_label);
    const std::string alt = sanitize_bitmap_text(post.media_alt_text.empty() ? "Preview unavailable for this media type." : post.media_alt_text);
    const int side_text_width = has_preview ? std::max(120, width - thumb_width - 18) : text_width;
    const int side_label_height = font_measure_wrapped(side_text_width, label.c_str(), 4);
    const int side_alt_height = font_measure_wrapped(side_text_width, alt.c_str(), 4);
    const bool stacked_layout = preview_text_needs_stacked_layout(side_label_height + 4 + side_alt_height,
                                                                  thumb_height,
                                                                  side_text_width,
                                                                  has_preview);
    if (has_preview) {
        fb_fill_rect(&app.text_fb,
                     thumb_x,
                     thumb_y,
                     thumb_width,
                     thumb_height,
                     COLOR_LGRAY);
        const image_t* thumb = image_cache_lookup(post.media_preview_url.c_str(), thumb_width, thumb_height);
        if (thumb) {
            const int draw_x = thumb_x + std::max(0, (thumb_width - thumb->width) / 2);
            const int draw_y = thumb_y + std::max(0, (thumb_height - thumb->height) / 2);
            fb_blit_rgba(&app.text_fb,
                         draw_x,
                         draw_y,
                         thumb->width,
                         thumb->height,
                         thumb->pixels);
        } else {
            const char* preview_label = "[preview]";
            font_draw_string(&app.text_fb,
                             thumb_x + std::max(0, (thumb_width - font_measure_string(preview_label)) / 2),
                             thumb_y + (thumb_height - app.text_fb.font_h) / 2,
                             preview_label,
                             kColorMeta,
                             COLOR_LGRAY);
        }
        draw_border(app, rewrite_rect_t{thumb_x, thumb_y, thumb_width, thumb_height}, kColorPostBorder, 1);
        if (stacked_layout) {
            text_y = thumb_y + thumb_height + 8;
        } else {
            text_x += thumb_width + 12;
            text_width = side_text_width;
        }
    }

    text_y = font_draw_wrapped(&app.text_fb,
                               text_x,
                               text_y,
                               text_width,
                               label.c_str(),
                               COLOR_BLACK,
                               COLOR_WHITE,
                               4);
    text_y += 4;
    font_draw_wrapped(&app.text_fb,
                      text_x,
                      text_y,
                      text_width,
                      alt.c_str(),
                      kColorMeta,
                      COLOR_WHITE,
                      4);

    y += height + 8;
}

int target_image_embed_width(const rewrite_app_t& app, int available_width) {
    const int screen_target = (app.framebuffer.info.screen_width * 7) / 10;
    return std::max(160, std::min(available_width, screen_target));
}

void queue_feed_image_requests(const rewrite_app_t& app) {
    if (app.feed_result.state != rewrite_feed_state_t::loaded) {
        return;
    }

    const int content_width = app.framebuffer.info.screen_width - (kOuterMargin * 2);
    const int avatar_block_width = app.profile_images_enabled ? (kFeedAvatarSize + kAvatarGap) : 0;
    const int post_text_width = std::max(160, content_width - avatar_block_width) - 8;
    const int count = std::min(6, static_cast<int>(app.feed_result.feed.items.size()));

    for (int index = 0; index < count; ++index) {
        const Bsky::Post& post = app.feed_result.feed.items[index];
        if (app.profile_images_enabled && !post.author.avatar_url.empty()) {
            image_cache_lookup(post.author.avatar_url.c_str(), kFeedAvatarSize, kFeedAvatarSize);
        }
        if (!app.embed_images_enabled) {
            continue;
        }

        if (!post.media_preview_url.empty()) {
            const int thumb_width = target_preview_thumb_width(app, post_text_width);
            const int thumb_height = std::max(96, (thumb_width * 3) / 4);
            image_cache_lookup(post.media_preview_url.c_str(), thumb_width, thumb_height);
        }
        if (!post.ext_thumb_url.empty()) {
            const int thumb_width = target_preview_thumb_width(app, post_text_width);
            const int thumb_height = std::max(96, (thumb_width * 3) / 4);
            image_cache_lookup(post.ext_thumb_url.c_str(), thumb_width, thumb_height);
        }
        if (!post.image_urls.empty()) {
            const int image_count = std::min(4, static_cast<int>(post.image_urls.size()));
            const int embed_width = target_image_embed_width(app, post_text_width);
            if (image_count == 1) {
                image_cache_lookup(post.image_urls[0].c_str(), embed_width, single_image_embed_height(app, embed_width));
            } else {
                const int tile_width = std::max(40, (embed_width - kEmbedImageGap) / 2);
                const int tile_height = image_embed_tile_height(app, embed_width, image_count);
                for (int image_index = 0; image_index < image_count; ++image_index) {
                    image_cache_lookup(post.image_urls[image_index].c_str(), tile_width, tile_height);
                }
            }
        }
    }
}

void wait_for_initial_feed_images(int max_wait_ms) {
    const auto start = std::chrono::steady_clock::now();
    bool saw_ready = false;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < max_wait_ms) {
        QCoreApplication::processEvents();
        if (image_cache_redraw_needed()) {
            saw_ready = true;
        }
        if (saw_ready && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >= 300) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

int single_image_embed_height(const rewrite_app_t& app, int embed_width) {
    const int screen_cap = std::max(kFeedEmbedMaxHeight, app.framebuffer.info.screen_height / 2);
    const int proportional_height = std::max(kFeedEmbedMaxHeight, (embed_width * 4) / 5);
    return std::min(screen_cap, proportional_height);
}

int image_embed_tile_height(const rewrite_app_t& app, int embed_width, int image_count) {
    if (image_count <= 1) {
        return single_image_embed_height(app, embed_width);
    }
    const int tile_width = std::max(40, (embed_width - kEmbedImageGap) / 2);
    const int max_tile_height = std::max(120, single_image_embed_height(app, embed_width) / 2);
    return std::min(max_tile_height, std::max(110, (tile_width * 4) / 5));
}

int measure_image_embed_height(const rewrite_app_t& app, int available_width, int image_count) {
    if (image_count <= 0) {
        return 0;
    }
    const int embed_width = target_image_embed_width(app, available_width);
    if (image_count == 1) {
        return single_image_embed_height(app, embed_width) + 8;
    }
    const int visible = std::min(image_count, 4);
    const int rows = (visible + 1) / 2;
    const int tile_height = image_embed_tile_height(app, embed_width, image_count);
    return (rows * tile_height) + ((rows - 1) * kEmbedImageGap) + 8;
}

void draw_image_embed_block(rewrite_app_t& app, const Bsky::Post& post, int x, int& y, int width) {
    const int image_count = std::min(static_cast<int>(post.image_urls.size()), 4);
    if (image_count <= 0) {
        return;
    }

    const int embed_width = target_image_embed_width(app, width);
    const int embed_x = x + std::max(0, (width - embed_width) / 2);

    if (image_count == 1) {
        const int embed_height = single_image_embed_height(app, embed_width);
        const image_t* embed = image_cache_lookup(post.image_urls[0].c_str(),
                                                  embed_width,
                                                  embed_height);
        if (embed) {
            const int draw_x = embed_x + std::max(0, (embed_width - embed->width) / 2);
            fb_blit_rgba(&app.text_fb,
                         draw_x,
                         y,
                         embed->width,
                         embed->height,
                         embed->pixels);
            y += embed->height + 8;
        } else {
            const int placeholder_width = embed_width;
            const int placeholder_x = embed_x;
            fb_fill_rect(&app.text_fb,
                         placeholder_x,
                         y,
                         placeholder_width,
                         embed_height,
                         COLOR_LGRAY);
            const char* label = "[image loading]";
            const int label_x = placeholder_x + std::max(0, (placeholder_width - font_measure_string(label)) / 2);
            font_draw_string(&app.text_fb,
                             label_x,
                             y + (embed_height - app.text_fb.font_h) / 2,
                             label,
                             kColorMeta,
                             COLOR_LGRAY);
            y += embed_height + 8;
        }
        return;
    }

    const int tile_width = std::max(40, (embed_width - kEmbedImageGap) / 2);
    const int tile_height = image_embed_tile_height(app, embed_width, image_count);
    const int grid_cols = std::min(2, image_count);
    const int grid_width = (grid_cols * tile_width) + ((grid_cols - 1) * kEmbedImageGap);
    const int grid_x = embed_x + std::max(0, (embed_width - grid_width) / 2);
    for (int index = 0; index < image_count; ++index) {
        const int col = index % 2;
        const int row = index / 2;
        int tile_x = grid_x + (col * (tile_width + kEmbedImageGap));
        const int tile_y = y + (row * (tile_height + kEmbedImageGap));
        if ((image_count % 2) == 1 && index == image_count - 1) {
            tile_x = embed_x + std::max(0, (embed_width - tile_width) / 2);
        }
        const image_t* embed = image_cache_lookup(post.image_urls[index].c_str(),
                                                  tile_width,
                                                  tile_height);
        if (embed) {
            const int draw_x = tile_x + std::max(0, (tile_width - embed->width) / 2);
            const int draw_y = tile_y + std::max(0, (tile_height - embed->height) / 2);
            fb_blit_rgba(&app.text_fb,
                         draw_x,
                         draw_y,
                         embed->width,
                         embed->height,
                         embed->pixels);
        } else {
            fb_fill_rect(&app.text_fb,
                         tile_x,
                         tile_y,
                         tile_width,
                         tile_height,
                         COLOR_LGRAY);
            const char* label = "[img]";
            const int label_x = tile_x + std::max(0, (tile_width - font_measure_string(label)) / 2);
            font_draw_string(&app.text_fb,
                             label_x,
                             tile_y + (tile_height - app.text_fb.font_h) / 2,
                             label,
                             kColorMeta,
                             COLOR_LGRAY);
        }
    }

    if (static_cast<int>(post.image_urls.size()) > image_count) {
        const int overlay_x = grid_x + tile_width + kEmbedImageGap;
        const int overlay_y = y + tile_height + kEmbedImageGap;
        const int overlay_w = tile_width;
        const int overlay_h = tile_height;
        rewrite_framebuffer_fill_rect(app.framebuffer,
                                      rewrite_rect_t{overlay_x, overlay_y, overlay_w, overlay_h},
                                      0x80);
        const std::string more = "+" + std::to_string(static_cast<int>(post.image_urls.size()) - image_count);
                        const int text_x = overlay_x + std::max(0, (overlay_w - font_measure_string(more.c_str())) / 2);
                        const int text_y = overlay_y + (overlay_h - app.text_fb.font_h) / 2;
                        font_draw_string(&app.text_fb,
                                 text_x,
                                 text_y,
                                 more.c_str(),
                                 COLOR_WHITE,
                                 0x80);
    }

                    y += measure_image_embed_height(app, width, image_count);
}

void draw_external_embed(rewrite_app_t& app, int x, int y, int width, const Bsky::Post& post, bool embed_images) {
    const int height = measure_external_embed_height(app, width, post, embed_images);
    draw_border(app, rewrite_rect_t{x, y, width, height}, kColorPostBorder, 1);

    const bool has_thumb = embed_images && !post.ext_thumb_url.empty();
    const bool alt_prefixed_description = post.ext_description.rfind("ALT:", 0) == 0;
    const bool title_only = alt_prefixed_description && !post.ext_title.empty();
    const int thumb_width = has_thumb ? target_preview_thumb_width(app, width) : 0;
    const int thumb_height = has_thumb ? std::max(96, (thumb_width * 3) / 4) : 0;
    const int thumb_x = x + 6;
    const int thumb_y = y + 6;
    int text_x = x + 6;
    int text_width = std::max(120, width - 12);
    int text_y = y + 6;
    const std::string title = sanitize_bitmap_text(post.ext_title.empty() ? "External media" : post.ext_title);
    const std::string description = title_only
        ? std::string{}
        : sanitize_bitmap_text(post.ext_description.empty() ? "External media preview" : post.ext_description);
    const int side_text_width = has_thumb ? std::max(120, width - thumb_width - 18) : text_width;
    const int side_title_height = font_measure_wrapped(side_text_width, title.c_str(), 4);
    const int side_description_height = description.empty() ? 0 : font_measure_wrapped(side_text_width, description.c_str(), 4);
    const bool stacked_layout = preview_text_needs_stacked_layout(side_title_height + (description.empty() ? 0 : (side_description_height + 4)),
                                                                  thumb_height,
                                                                  side_text_width,
                                                                  has_thumb);
    if (has_thumb) {
        fb_fill_rect(&app.text_fb,
                     thumb_x,
                     thumb_y,
                     thumb_width,
                     thumb_height,
                     COLOR_LGRAY);
        const image_t* thumb = image_cache_lookup(post.ext_thumb_url.c_str(),
                                                  thumb_width,
                                                  thumb_height);
        if (thumb) {
            const int draw_x = thumb_x + std::max(0, (thumb_width - thumb->width) / 2);
            const int draw_y = thumb_y + std::max(0, (thumb_height - thumb->height) / 2);
            fb_blit_rgba(&app.text_fb,
                         draw_x,
                         draw_y,
                         thumb->width,
                         thumb->height,
                         thumb->pixels);
        } else {
            const char* label = "[preview]";
            font_draw_string(&app.text_fb,
                             thumb_x + std::max(0, (thumb_width - font_measure_string(label)) / 2),
                             thumb_y + (thumb_height - app.text_fb.font_h) / 2,
                             label,
                             kColorMeta,
                             COLOR_LGRAY);
        }
        draw_border(app, rewrite_rect_t{thumb_x, thumb_y, thumb_width, thumb_height}, kColorPostBorder, 1);
        if (stacked_layout) {
            text_y = thumb_y + thumb_height + 8;
        } else {
            text_x += thumb_width + 12;
            text_width = side_text_width;
        }
    }

    text_y = font_draw_wrapped(&app.text_fb,
                               text_x,
                               text_y,
                               text_width,
                               title.c_str(),
                               COLOR_BLACK,
                               COLOR_WHITE,
                               4);
    if (!title_only) {
        if (!description.empty()) {
            text_y += 4;
            font_draw_wrapped(&app.text_fb,
                              text_x,
                              text_y,
                              text_width,
                              description.c_str(),
                              kColorMeta,
                              COLOR_WHITE,
                              4);
        }
    }
}

int measure_embed_block_height(const rewrite_app_t& app, const Bsky::Post& post, int width) {
    switch (post.embed_type) {
        case Bsky::EmbedType::Image:
            return app.embed_images_enabled
                ? measure_image_embed_height(app, width, static_cast<int>(post.image_urls.size()))
                : measure_image_alt_height(app, post, width) + 8;
        case Bsky::EmbedType::Quote:
            return post.quoted_post ? measure_quote_embed_height(app, width, post.quoted_post.get()) + 8 : 0;
        case Bsky::EmbedType::External:
            return measure_external_embed_height(app, width, post, app.embed_images_enabled) + 8;
        case Bsky::EmbedType::Video:
            return measure_unsupported_media_height(app, post, width) + 8;
        case Bsky::EmbedType::RecordWithMedia: {
            int total = 0;
            if (!post.image_urls.empty()) {
                total += app.embed_images_enabled
                    ? measure_image_embed_height(app, width, static_cast<int>(post.image_urls.size()))
                    : measure_image_alt_height(app, post, width) + 8;
            }
            if (!post.media_preview_url.empty() || !post.media_alt_text.empty() || !post.media_label.empty()) {
                total += measure_unsupported_media_height(app, post, width) + 8;
            }
            if (post.quoted_post) total += measure_quote_embed_height(app, width, post.quoted_post.get()) + 8;
            else if (!post.ext_uri.empty()) total += measure_external_embed_height(app, width, post, app.embed_images_enabled) + 8;
            return total;
        }
        default:
            return 0;
    }
}

void draw_embed_block(rewrite_app_t& app, const Bsky::Post& post, int x, int& y, int width) {
    if ((post.embed_type == Bsky::EmbedType::Image || post.embed_type == Bsky::EmbedType::RecordWithMedia) &&
        !post.image_urls.empty()) {
        if (app.embed_images_enabled) {
            draw_image_embed_block(app, post, x, y, width);
        } else {
            draw_image_alt_block(app, post, x, y, width);
        }
    }

    if ((post.embed_type == Bsky::EmbedType::Video || post.embed_type == Bsky::EmbedType::RecordWithMedia) &&
        (!post.media_preview_url.empty() || !post.media_alt_text.empty() || !post.media_label.empty())) {
        draw_unsupported_media_block(app, post, x, y, width);
    }

    if ((post.embed_type == Bsky::EmbedType::Quote || post.embed_type == Bsky::EmbedType::RecordWithMedia) && post.quoted_post) {
        draw_quote_embed(app, x, y, std::max(80, width - kQuoteIndent), post.quoted_post.get());
        y += measure_quote_embed_height(app, std::max(80, width - kQuoteIndent), post.quoted_post.get()) + 8;
    } else if ((post.embed_type == Bsky::EmbedType::External || post.embed_type == Bsky::EmbedType::RecordWithMedia) && !post.ext_uri.empty()) {
        draw_external_embed(app, x, y, width, post, app.embed_images_enabled);
        y += measure_external_embed_height(app, width, post, app.embed_images_enabled) + 8;
    }
}

std::string rewrite_dir() {
    const char* value = std::getenv("SKEETS_REWRITE_DIR");
    return value && *value ? value : "/mnt/onboard/.adds/sKeets-rewrite";
}

std::string rewrite_asset_path(const char* filename) {
    return rewrite_dir() + "/assets/" + filename;
}

bool load_local_image_asset(image_t& image, const std::string& path, int max_w, int max_h) {
    image_t loaded{};
    if (image_load_file(path.c_str(), &loaded) != 0) {
        return false;
    }
    if (max_w > 0 && max_h > 0) {
        image_scale_to_fit(&loaded, max_w, max_h);
    }
    image_free(&image);
    image = loaded;
    return true;
}

void load_brand_assets(rewrite_app_t& app) {
    load_local_image_asset(app.header_logo,
                           rewrite_asset_path("sKeets_top_bar_white.png"),
                           kHeaderLogoMaxWidth,
                           kHeaderLogoMaxHeight);
    load_local_image_asset(app.splash_logo,
                           rewrite_asset_path("sKeets_splash_white.png"),
                           kSplashLogoMaxWidth,
                           kSplashLogoMaxHeight);
}

void free_brand_assets(rewrite_app_t& app) {
    image_free(&app.header_logo);
    image_free(&app.splash_logo);
}

void draw_centered_image(rewrite_app_t& app, const image_t& image, int center_x, int y) {
    if (!image.pixels || image.width <= 0 || image.height <= 0) {
        return;
    }
    const int draw_x = std::max(0, center_x - (image.width / 2));
    fb_blit_rgba(&app.text_fb, draw_x, y, image.width, image.height, image.pixels);
}

void draw_header_brand(rewrite_app_t& app, const rewrite_rect_t& header_rect) {
    if (app.header_logo.pixels && app.header_logo.width > 0 && app.header_logo.height > 0) {
        const int logo_y = header_rect.y + std::max(0, (header_rect.height - app.header_logo.height) / 2);
        draw_centered_image(app, app.header_logo, header_rect.x + (header_rect.width / 2), logo_y);
        return;
    }

    const int brand_y = header_rect.y + std::max(0, (header_rect.height - line_height(rewrite_text_role_t::fallback_brand)) / 2);
    draw_text(app,
              std::max(kOuterMargin,
                       header_rect.x + (header_rect.width - measure_text("sKeets", rewrite_text_role_t::fallback_brand)) / 2),
              brand_y,
              "sKeets",
              rewrite_text_role_t::fallback_brand,
              COLOR_WHITE,
              kColorHeader);
}

std::string read_revision_summary() {
    std::ifstream file(rewrite_dir() + "/package-revision.txt");
    if (!file) {
        return "revision unavailable";
    }

    std::string line;
    std::string version;
    std::string build_timestamp;
    while (std::getline(file, line)) {
        if (line.rfind("version=", 0) == 0) {
            version = line.substr(8);
        } else if (line.rfind("build_timestamp=", 0) == 0) {
            build_timestamp = line.substr(16);
        }
    }

    if (version.empty() && build_timestamp.empty()) {
        return "revision metadata unavailable";
    }
    if (build_timestamp.empty()) {
        return "version " + version;
    }
    if (version.empty()) {
        return build_timestamp;
    }
    return "version " + version + " | " + build_timestamp;
}

rewrite_input_protocol_t detect_input_protocol() {
    const char* value = std::getenv("SKEETS_REWRITE_TOUCH_PROTOCOL");
    if (!value || !*value) return rewrite_input_protocol_t::standard_multitouch;
    if (std::string(value) == "snow") return rewrite_input_protocol_t::snow;
    return rewrite_input_protocol_t::standard_multitouch;
}

rewrite_refresh_mode_t ui_refresh_mode(const rewrite_app_t& app, bool full_refresh) {
    if (full_refresh) {
        return rewrite_refresh_mode_t::full;
    }
    if (app.framebuffer.info.supports_grayscale_partial_refresh) {
        return rewrite_refresh_mode_t::grayscale_partial;
    }
    return rewrite_refresh_mode_t::partial;
}

rewrite_rect_t full_screen_rect(const rewrite_app_t& app) {
    return rewrite_rect_t{0, 0, app.framebuffer.info.screen_width, app.framebuffer.info.screen_height};
}

bool contains_point(const rewrite_rect_t& rect, int x, int y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

void draw_border(rewrite_app_t& app, const rewrite_rect_t& rect, std::uint8_t color, int thickness) {
    rewrite_framebuffer_fill_rect(app.framebuffer, rewrite_rect_t{rect.x, rect.y, rect.width, thickness}, color);
    rewrite_framebuffer_fill_rect(app.framebuffer, rewrite_rect_t{rect.x, rect.y + rect.height - thickness, rect.width, thickness}, color);
    rewrite_framebuffer_fill_rect(app.framebuffer, rewrite_rect_t{rect.x, rect.y, thickness, rect.height}, color);
    rewrite_framebuffer_fill_rect(app.framebuffer, rewrite_rect_t{rect.x + rect.width - thickness, rect.y, thickness, rect.height}, color);
}

void draw_centered_text(rewrite_app_t& app,
                        int center_x,
                        int y,
                        const std::string& text,
                        std::uint8_t fg,
                        std::uint8_t bg,
                        rewrite_text_role_t role = rewrite_text_role_t::body) {
    const int text_width = measure_text(text, role);
    const int start_x = std::max(kOuterMargin, center_x - text_width / 2);
    draw_text(app, start_x, y, text, role, fg, bg);
}

void draw_card(rewrite_app_t& app,
               const rewrite_rect_t& rect,
               const std::string& title,
               const std::vector<std::string>& lines) {
    rewrite_framebuffer_fill_rect(app.framebuffer, rect, kColorCard);
    draw_border(app, rect, kColorCardBorder);

    const rewrite_rect_t title_rect{rect.x, rect.y, rect.width, line_height(rewrite_text_role_t::card_title) + kCardPadding};
    rewrite_framebuffer_fill_rect(app.framebuffer, title_rect, kColorCardTitle);
    draw_text(app,
              rect.x + kCardPadding,
              rect.y + kCardPadding / 2,
              title,
              rewrite_text_role_t::card_title,
              COLOR_WHITE,
              kColorCardTitle);

    int cursor_y = rect.y + title_rect.height + 12;
    const int max_width = rect.width - (kCardPadding * 2);
    for (const std::string& line : lines) {
        cursor_y = draw_wrapped_text(app,
                                     rect.x + kCardPadding,
                                     cursor_y,
                                     max_width,
                                     line,
                                     rewrite_text_role_t::body,
                                     COLOR_BLACK,
                                     kColorCard);
        cursor_y += 8;
        if (cursor_y >= rect.y + rect.height - line_height(rewrite_text_role_t::body) - kCardPadding) {
            break;
        }
    }
}

void draw_button(rewrite_app_t& app, const rewrite_button_t& button, std::uint8_t fill) {
    rewrite_framebuffer_fill_rect(app.framebuffer, button.rect, fill);
    draw_border(app, button.rect, COLOR_BLACK, 3);

    const int center_y = button.rect.y + (button.rect.height - line_height(rewrite_text_role_t::button)) / 2;
    draw_centered_text(app,
                       button.rect.x + button.rect.width / 2,
                       center_y,
                       button.label,
                       fill <= 0x40 ? COLOR_WHITE : COLOR_BLACK,
                       fill,
                       rewrite_text_role_t::button);
}

void refresh_runtime_state(rewrite_app_t& app) {
    app.power = rewrite_probe_power();
    app.network = rewrite_probe_network();
}

void refresh_bootstrap_state(rewrite_app_t& app) {
    app.bootstrap = rewrite_run_bootstrap();
    app.status_line = app.bootstrap.headline;
    app.input_line = app.bootstrap.detail;
}

void load_settings(rewrite_app_t& app) {
    config_t* config = config_open(skeets_config_path());
    if (!config) {
        return;
    }
    app.profile_images_enabled = config_get_bool(config, "profile_images_enabled", true);
    app.embed_images_enabled = config_get_bool(config, "embed_images_enabled", false);
    app.allow_nudity_content = config_get_bool(config, "allow_nudity_content", false);
    app.allow_porn_content = config_get_bool(config, "allow_porn_content", false);
    app.allow_suggestive_content = config_get_bool(config, "allow_suggestive_content", false);
    config_free(config);
}

void save_settings(const rewrite_app_t& app) {
    config_t* config = config_open(skeets_config_path());
    if (!config) {
        return;
    }
    config_set_bool(config, "profile_images_enabled", app.profile_images_enabled);
    config_set_bool(config, "embed_images_enabled", app.embed_images_enabled);
    config_set_bool(config, "allow_nudity_content", app.allow_nudity_content);
    config_set_bool(config, "allow_porn_content", app.allow_porn_content);
    config_set_bool(config, "allow_suggestive_content", app.allow_suggestive_content);
    config_save(config);
    config_free(config);
}

void clear_saved_session(rewrite_app_t& app) {
    config_t* config = config_open(skeets_config_path());
    if (config) {
        config_set_str(config, "handle", "");
        config_set_str(config, "access_jwt", "");
        config_set_str(config, "refresh_jwt", "");
        config_set_str(config, "did", "");
        config_set_str(config, "pds_url", "");
        config_set_str(config, "appview_url", "");
        config_save(config);
        config_free(config);
    }

    app.bootstrap = rewrite_bootstrap_result_t{};
    app.feed_result = rewrite_feed_result_t{};
    app.thread_result = rewrite_thread_result_t{};
    app.feed_page_start = 0;
    app.feed_page_count = 0;
    app.feed_page_history.clear();
    app.thread_page_start = 0;
    app.thread_page_count = 0;
    app.visible_post_hits.clear();
    app.thread_post_hits.clear();
    app.selected_post_uri.clear();
}

void persist_session(rewrite_app_t& app, const Bsky::Session& session) {
    config_t* config = config_open(skeets_config_path());
    if (config) {
        config_set_str(config, "handle", session.handle.c_str());
        config_set_str(config, "access_jwt", session.access_jwt.c_str());
        config_set_str(config, "refresh_jwt", session.refresh_jwt.c_str());
        config_set_str(config, "did", session.did.c_str());
        config_set_str(config, "pds_url", session.pds_url.c_str());
        config_set_str(config, "appview_url", session.appview_url.c_str());
        config_save(config);
        config_free(config);
    }
    app.bootstrap.session = session;
}

void apply_updated_session(rewrite_app_t& app, const Bsky::Session& session, bool updated) {
    if (!updated) {
        return;
    }
    persist_session(app, session);
}

void open_settings(rewrite_app_t& app, rewrite_view_mode_t return_view) {
    app.settings_return_view = return_view;
    app.view_mode = rewrite_view_mode_t::settings;
    app.status_line = "Settings";
    app.input_line = "Toggle image and moderation preferences or sign out";
}

bool label_matches_nudity(const std::string& label) {
    return label == "nudity";
}

bool label_matches_porn(const std::string& label) {
    return label == "porn";
}

bool label_matches_suggestive(const std::string& label) {
    return label == "suggestive" || label == "sexual";
}

std::string moderation_hide_reason(const rewrite_app_t& app, const Bsky::Post& post) {
    bool hide_nudity = false;
    bool hide_porn = false;
    bool hide_suggestive = false;
    for (const auto& raw_label : post.moderation_labels) {
        const std::string label = to_lower_ascii(raw_label);
        if (!app.allow_nudity_content && label_matches_nudity(label)) {
            hide_nudity = true;
        }
        if (!app.allow_porn_content && label_matches_porn(label)) {
            hide_porn = true;
        }
        if (!app.allow_suggestive_content && label_matches_suggestive(label)) {
            hide_suggestive = true;
        }
    }

    std::string reason;
    if (hide_nudity) reason += (reason.empty() ? "" : ", ") + std::string("Nudity");
    if (hide_porn) reason += (reason.empty() ? "" : ", ") + std::string("Porn");
    if (hide_suggestive) reason += (reason.empty() ? "" : ", ") + std::string("Suggestive");
    return reason;
}

std::string hidden_post_message(const rewrite_app_t& app, const Bsky::Post& post) {
    const std::string reason = moderation_hide_reason(app, post);
    if (reason.empty()) {
        return {};
    }
    return "Hidden content: " + reason + ". Enable this category in Settings to view it.";
}

void load_feed(rewrite_app_t& app) {
    if (!app.bootstrap.authenticated) return;
    app.status_line = "Loading timeline...";
    app.feed_result = rewrite_fetch_feed(app.bootstrap.session);
    apply_updated_session(app, app.feed_result.session, app.feed_result.session_updated);
    app.feed_page_start = 0;
    app.feed_page_count = 0;
    app.feed_page_history.clear();
    if (app.feed_result.state == rewrite_feed_state_t::loaded) {
        app.status_line = std::to_string(app.feed_result.post_count) + " posts loaded";
        app.input_line = "Tap a post area for details, or use the buttons below";
    } else {
        app.status_line = "Feed load failed";
        app.input_line = app.feed_result.error_message;
    }
}

void load_thread(rewrite_app_t& app, const std::string& post_uri) {
    app.selected_post_uri = post_uri;
    app.thread_result = rewrite_fetch_thread(app.bootstrap.session, post_uri);
    apply_updated_session(app, app.thread_result.session, app.thread_result.session_updated);
    app.thread_page_start = 0;
    app.thread_page_count = 0;
    if (app.thread_result.state == rewrite_thread_state_t::loaded) {
        app.status_line = "Thread loaded";
        app.input_line = "Tap < Back to return to the feed, or use the right scrollbar for longer threads";
    } else {
        app.status_line = "Thread load failed";
        app.input_line = app.thread_result.error_message;
    }
}

bool advance_feed_page(rewrite_app_t& app) {
    const int total = static_cast<int>(app.feed_result.feed.items.size());
    int next_start = app.feed_page_start + app.feed_page_count;
    if (next_start < total) {
        app.feed_page_history.push_back(app.feed_page_start);
        app.feed_page_start = next_start;
        return true;
    }

    if (!app.feed_result.feed.cursor.empty()) {
        auto more = rewrite_fetch_feed(app.bootstrap.session, 30, app.feed_result.feed.cursor);
        apply_updated_session(app, more.session, more.session_updated);
        if (more.state == rewrite_feed_state_t::loaded) {
            app.feed_page_history.push_back(app.feed_page_start);
            app.feed_page_start = total;
            for (auto& p : more.feed.items) {
                app.feed_result.feed.items.push_back(std::move(p));
            }
            app.feed_result.feed.cursor = more.feed.cursor;
            app.feed_result.post_count = static_cast<int>(app.feed_result.feed.items.size());
            return true;
        }
    }

    return false;
}

void flatten_thread_posts(const Bsky::Post& post,
                          int depth,
                          std::vector<std::pair<const Bsky::Post*, int>>& out) {
    out.push_back({&post, depth});
    for (const auto& reply : post.replies) {
        if (reply) flatten_thread_posts(*reply, depth + 1, out);
    }
}

std::string feed_like_label(const Bsky::Post& post) {
    return std::string(post.viewer_like.empty() ? "<Like> " : ">Like< ") + std::to_string(post.like_count);
}

std::string feed_reply_label(const Bsky::Post& post) {
    return "<Reply> " + std::to_string(post.reply_count);
}

std::string feed_repost_label(const Bsky::Post& post) {
    return std::string(post.viewer_repost.empty() ? "<Repost> " : ">Repost< ") + std::to_string(post.repost_count);
}

std::string thread_like_label(const Bsky::Post& post) {
    return std::string(post.viewer_like.empty() ? "<Like> " : ">Like< ") + std::to_string(post.like_count);
}

std::string thread_reply_label(const Bsky::Post& post) {
    return "<Reply> " + std::to_string(post.reply_count);
}

std::string thread_repost_label(const Bsky::Post& post) {
    return std::string(post.viewer_repost.empty() ? "<Repost> " : ">Repost< ") + std::to_string(post.repost_count);
}

int measure_thread_post_height(const rewrite_app_t& app,
                               const Bsky::Post& post,
                               bool include_indent_bar,
                               int text_width_for_post) {
    int needed = 0;
    if (include_indent_bar) {
        needed += 0;
    }
    needed += std::max(app.text_fb.font_h + 4,
                       app.profile_images_enabled ? kThreadAvatarSize + 4 : 0);
    const std::string hidden_message = hidden_post_message(app, post);
    if (!hidden_message.empty()) {
        needed += font_measure_wrapped(text_width_for_post,
                                       hidden_message.c_str(),
                                       4);
        needed += 4;
    } else if (!post.text.empty()) {
        const std::string text = sanitize_bitmap_text(post.text);
        needed += font_measure_wrapped(text_width_for_post,
                                       text.c_str(),
                                       4);
        needed += 4;
        needed += measure_embed_block_height(app, post, text_width_for_post);
    }
    needed += app.text_fb.font_h + 8;
    needed += 1;
    needed += 12;
    return needed;
}

int clamp_thread_page_start(int start, int total) {
    if (total <= 0) {
        return 0;
    }
    return std::max(0, std::min(start, total - 1));
}

rewrite_rect_t make_stat_hit_rect(int x, int y, int width, int height) {
    return rewrite_rect_t{x, std::max(0, y - 6), width, height + 12};
}

rewrite_rect_t expand_rect(const rewrite_rect_t& rect, int pad, int max_w, int max_h) {
    rewrite_rect_t out = rect;
    out.x = std::max(0, out.x - pad);
    out.y = std::max(0, out.y - pad);
    out.width = std::min(max_w - out.x, out.width + pad * 2);
    out.height = std::min(max_h - out.y, out.height + pad * 2);
    return out;
}

rewrite_refresh_mode_t stats_refresh_mode(const rewrite_app_t& app) {
    return app.framebuffer.info.supports_grayscale_partial_refresh
        ? rewrite_refresh_mode_t::grayscale_partial
        : rewrite_refresh_mode_t::partial;
}

void refresh_region(rewrite_app_t& app, const rewrite_rect_t& rect) {
    std::string error_message;
    if (!rewrite_framebuffer_refresh(app.framebuffer,
                                     stats_refresh_mode(app),
                                     rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: partial refresh failed: %s\n", error_message.c_str());
    }
}

void render_feed_stats_only(rewrite_app_t& app, const rewrite_post_hit_t& hit) {
    if (hit.post_index < 0 || hit.post_index >= static_cast<int>(app.feed_result.feed.items.size())) {
        return;
    }
    const Bsky::Post& post = app.feed_result.feed.items[hit.post_index];
    const std::string stats = feed_like_label(post) + "  " + feed_reply_label(post) + "  " + feed_repost_label(post);
    rewrite_framebuffer_fill_rect(app.framebuffer, hit.stats_rect, COLOR_WHITE);
    draw_text(app,
              hit.stats_x,
              hit.stats_y,
              stats,
              rewrite_text_role_t::meta,
              kColorMeta,
              COLOR_WHITE);
    refresh_region(app,
                   expand_rect(hit.stats_rect,
                               6,
                               app.framebuffer.info.screen_width,
                               app.framebuffer.info.screen_height));
}

void render_thread_stats_only(rewrite_app_t& app, const rewrite_thread_post_hit_t& hit) {
    if (!hit.post) {
        return;
    }
    const std::string stats = thread_like_label(*hit.post) + "  " +
                              thread_reply_label(*hit.post) + "  " +
                              thread_repost_label(*hit.post);
    rewrite_framebuffer_fill_rect(app.framebuffer, hit.stats_rect, COLOR_WHITE);
    draw_text(app,
              hit.stats_x,
              hit.stats_y,
              stats,
              rewrite_text_role_t::meta,
              kColorMeta,
              COLOR_WHITE);
    refresh_region(app,
                   expand_rect(hit.stats_rect,
                               6,
                               app.framebuffer.info.screen_width,
                               app.framebuffer.info.screen_height));
}

std::vector<std::string> auth_lines(const rewrite_app_t& app) {
    return rewrite_bootstrap_lines(app.bootstrap);
}

std::vector<std::string> device_lines(const rewrite_app_t& app) {
    return {
        "Product: " + (app.device.product_name.empty() ? std::string("unknown") : app.device.product_name),
        "Codename: " + (app.device.codename.empty() ? std::string("unknown") : app.device.codename),
        "Platform: " + (app.device.platform.empty() ? std::string("unknown") : app.device.platform),
        "Input protocol: " + (app.device.touch_protocol.empty() ? std::string("unknown") : app.device.touch_protocol),
        "Panel flags: color=" + bool_label(app.device.is_color) + " mtk=" + bool_label(app.device.is_mtk) + " smp=" + bool_label(app.device.is_smp),
    };
}

std::vector<std::string> network_lines(const rewrite_app_t& app) {
    std::string addresses = app.network.addresses.empty() ? "none" : app.network.addresses.front();
    if (app.network.addresses.size() > 1) {
        addresses += " (+" + std::to_string(app.network.addresses.size() - 1) + " more)";
    }

    return {
        "Interface: " + (app.network.interface_name.empty() ? std::string("unknown") : app.network.interface_name),
        "Carrier: " + bool_label(app.network.carrier_up) + " | operstate: " + bool_label(app.network.operstate_up),
        "IPv4 default route: " + bool_label(app.network.has_default_ipv4_route),
        "DNS resolution: " + bool_label(app.network.can_resolve_dns),
        "Addresses: " + addresses,
        "Online: " + bool_label(app.network.online),
    };
}

std::vector<std::string> power_lines(const rewrite_app_t& app) {
    std::string capacity = app.power.capacity_percent >= 0
        ? std::to_string(app.power.capacity_percent) + "%"
        : "unknown";

    return {
        "Battery path: " + (app.power.battery_sysfs.empty() ? std::string("unknown") : app.power.battery_sysfs),
        "Capacity: " + capacity + " | status: " + (app.power.status.empty() ? std::string("unknown") : app.power.status),
        "Charging: " + bool_label(app.power.is_charging) + " | charged: " + bool_label(app.power.is_charged),
        "Suspend safe: " + bool_label(app.power.suspend_safe),
        "Suspend reason: " + (app.power.suspend_reason.empty() ? std::string("none") : app.power.suspend_reason),
        "Build: " + app.revision_summary,
    };
}

void render_screen(rewrite_app_t& app, bool full_refresh) {
    const int width = app.framebuffer.info.screen_width;
    const int height = app.framebuffer.info.screen_height;
    const int header_height = std::max(108, height / 11);
    const int button_height = std::max(110, height / 10);
    const int status_height = std::max(132, height / 10);
    const int cards_height = height - header_height - button_height - status_height - (kOuterMargin * 5);
    const int row_height = std::max(180, cards_height / 2);
    const int half_width = (width - (kOuterMargin * 2) - kCardGap) / 2;

    const rewrite_rect_t auth_rect{kOuterMargin, header_height + kOuterMargin, half_width, row_height};
    const rewrite_rect_t device_rect{auth_rect.x + auth_rect.width + kCardGap, auth_rect.y, half_width, row_height};
    const rewrite_rect_t power_rect{kOuterMargin, auth_rect.y + row_height + kCardGap, half_width, row_height};
    const rewrite_rect_t network_rect{power_rect.x + power_rect.width + kCardGap, power_rect.y, half_width, row_height};
    const rewrite_rect_t status_rect{kOuterMargin,
                                     network_rect.y + network_rect.height + kOuterMargin,
                                     width - (kOuterMargin * 2),
                                     status_height};
    const int button_width = (width - (kOuterMargin * 2) - kCardGap) / 2;
    app.refresh_button = rewrite_button_t{{kOuterMargin, height - button_height - kOuterMargin, button_width, button_height}, "Recheck"};
    app.exit_button = rewrite_button_t{{app.refresh_button.rect.x + button_width + kCardGap,
                                        app.refresh_button.rect.y,
                                        button_width,
                                        button_height},
                                       "Exit"};

    rewrite_framebuffer_clear(app.framebuffer, COLOR_WHITE);

    const rewrite_rect_t header_rect{0, 0, width, header_height};
    rewrite_framebuffer_fill_rect(app.framebuffer, header_rect, kColorHeader);
    draw_header_brand(app, header_rect);

    draw_card(app, auth_rect, "Authentication", auth_lines(app));
    draw_card(app, device_rect, "Device", device_lines(app));
    draw_card(app, power_rect, "Power And Runtime", power_lines(app));
    draw_card(app, network_rect, "Connectivity", network_lines(app));

    rewrite_framebuffer_fill_rect(app.framebuffer, status_rect, kColorStatus);
    draw_border(app, status_rect, kColorCardBorder);
    draw_text(app,
              status_rect.x + kCardPadding,
              status_rect.y + 16,
              app.status_line,
              rewrite_text_role_t::status_title,
              COLOR_BLACK,
              kColorStatus);
    draw_wrapped_text(app,
                      status_rect.x + kCardPadding,
                      status_rect.y + 16 + line_height(rewrite_text_role_t::status_title) + 12,
                      status_rect.width - (kCardPadding * 2),
                      app.input_line,
                      rewrite_text_role_t::status_detail,
                      COLOR_BLACK,
                      kColorStatus);

    draw_button(app, app.refresh_button, kColorButtonSecondary);
    draw_button(app, app.exit_button, kColorButtonPrimary);

    std::string error_message;
    const rewrite_rect_t screen_rect = full_screen_rect(app);
    if (!rewrite_framebuffer_refresh(app.framebuffer,
                                     ui_refresh_mode(app, full_refresh),
                                     screen_rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: refresh failed: %s\n", error_message.c_str());
    }
}

void render_loading_screen(rewrite_app_t& app,
                           const std::string& title,
                           const std::string& detail,
                           bool full_refresh) {
    const int width = app.framebuffer.info.screen_width;
    const int height = app.framebuffer.info.screen_height;
    const int splash_top = std::max(120, height / 7);

    rewrite_framebuffer_clear(app.framebuffer, COLOR_WHITE);

    if (app.splash_logo.pixels && app.splash_logo.width > 0 && app.splash_logo.height > 0) {
        draw_centered_image(app, app.splash_logo, width / 2, splash_top);
    } else {
        draw_centered_text(app,
                           width / 2,
                           splash_top + 24,
                           "sKeets",
                           kColorHeader,
                           COLOR_WHITE,
                           rewrite_text_role_t::splash_title);
    }

    const int logo_bottom = splash_top + (app.splash_logo.height > 0 ? app.splash_logo.height : line_height(rewrite_text_role_t::splash_title));
    const int title_y = logo_bottom + 54;
    draw_centered_text(app,
                       width / 2,
                       title_y,
                       title,
                       COLOR_BLACK,
                       COLOR_WHITE,
                       rewrite_text_role_t::splash_title);
    draw_wrapped_text(app,
                      kOuterMargin * 2,
                      title_y + line_height(rewrite_text_role_t::splash_title) + 20,
                      width - (kOuterMargin * 4),
                      detail,
                      rewrite_text_role_t::splash_detail,
                      kColorMeta,
                      COLOR_WHITE);

    std::string error_message;
    const rewrite_rect_t screen_rect = full_screen_rect(app);
    if (!rewrite_framebuffer_refresh(app.framebuffer,
                                     ui_refresh_mode(app, full_refresh),
                                     screen_rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: loading refresh failed: %s\n", error_message.c_str());
    }
}

void render_diagnostics_screen(rewrite_app_t& app, bool full_refresh) {
    const int width = app.framebuffer.info.screen_width;
    const int height = app.framebuffer.info.screen_height;
    const int header_height = std::max(108, height / 11);
    const int button_height = std::max(110, height / 10);
    const int status_height = std::max(132, height / 10);
    const int cards_height = height - header_height - button_height - status_height - (kOuterMargin * 5);
    const int row_height = std::max(180, cards_height / 2);
    const int half_width = (width - (kOuterMargin * 2) - kCardGap) / 2;

    const rewrite_rect_t auth_rect{kOuterMargin, header_height + kOuterMargin, half_width, row_height};
    const rewrite_rect_t device_rect{auth_rect.x + auth_rect.width + kCardGap, auth_rect.y, half_width, row_height};
    const rewrite_rect_t power_rect{kOuterMargin, auth_rect.y + row_height + kCardGap, half_width, row_height};
    const rewrite_rect_t network_rect{power_rect.x + power_rect.width + kCardGap, power_rect.y, half_width, row_height};
    const rewrite_rect_t status_rect{kOuterMargin,
                                     network_rect.y + network_rect.height + kOuterMargin,
                                     width - (kOuterMargin * 2),
                                     status_height};
    const int button_width = (width - (kOuterMargin * 2) - kCardGap) / 2;
    app.diagnostics_back_button = {{kOuterMargin, height - button_height - kOuterMargin, button_width, button_height}, "< Back"};
    app.diagnostics_refresh_button = {{app.diagnostics_back_button.rect.x + button_width + kCardGap,
                                       app.diagnostics_back_button.rect.y,
                                       button_width,
                                       button_height},
                                      "Refresh"};

    rewrite_framebuffer_clear(app.framebuffer, COLOR_WHITE);
    const rewrite_rect_t header_rect{0, 0, width, header_height};
    rewrite_framebuffer_fill_rect(app.framebuffer, header_rect, kColorHeader);
    draw_header_brand(app, header_rect);

    draw_card(app, auth_rect, "Authentication", auth_lines(app));
    draw_card(app, device_rect, "Device", device_lines(app));
    draw_card(app, power_rect, "Power And Runtime", power_lines(app));
    draw_card(app, network_rect, "Connectivity", network_lines(app));

    rewrite_framebuffer_fill_rect(app.framebuffer, status_rect, kColorStatus);
    draw_border(app, status_rect, kColorCardBorder);
    draw_text(app,
              status_rect.x + kCardPadding,
              status_rect.y + 16,
              app.status_line,
              rewrite_text_role_t::status_title,
              COLOR_BLACK,
              kColorStatus);
    draw_wrapped_text(app,
                      status_rect.x + kCardPadding,
                      status_rect.y + 16 + line_height(rewrite_text_role_t::status_title) + 12,
                      status_rect.width - (kCardPadding * 2),
                      app.input_line,
                      rewrite_text_role_t::status_detail,
                      COLOR_BLACK,
                      kColorStatus);

    draw_button(app, app.diagnostics_back_button, kColorButtonSecondary);
    draw_button(app, app.diagnostics_refresh_button, kColorButtonPrimary);

    std::string error_message;
    const rewrite_rect_t screen_rect = full_screen_rect(app);
    if (!rewrite_framebuffer_refresh(app.framebuffer,
                                     ui_refresh_mode(app, full_refresh),
                                     screen_rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: diagnostics refresh failed: %s\n", error_message.c_str());
    }
}

void render_feed_screen(rewrite_app_t& app, bool full_refresh) {
    const int width = app.framebuffer.info.screen_width;
    const int height = app.framebuffer.info.screen_height;
    const int header_height = std::max(72, height / 14);
    const int button_height = std::max(90, height / 12);
    const int content_top = header_height;
    const int content_bottom = height - button_height - kOuterMargin;
    const int content_width = width - (kOuterMargin * 2);
    const int avatar_block_width = app.profile_images_enabled ? (kFeedAvatarSize + kAvatarGap) : 0;
    const int text_left = kOuterMargin + avatar_block_width;
    const int post_text_width = std::max(160, content_width - avatar_block_width);
    const int line_spacing = 4;

    // Layout bottom buttons: [< Prev] [Refresh] [Next >]
    const int btn_count = 3;
    const int btn_gap = kCardGap;
    const int btn_width = (width - (kOuterMargin * 2) - (btn_gap * (btn_count - 1))) / btn_count;
    const int btn_y = height - button_height - kOuterMargin;
    app.feed_back_button = {
        {kOuterMargin, btn_y, btn_width, button_height},
        "< Prev"
    };
    app.feed_refresh_button = {
        {kOuterMargin + btn_width + btn_gap, btn_y, btn_width, button_height},
        "Refresh"
    };
    app.feed_next_button = {
        {kOuterMargin + (btn_width + btn_gap) * 2, btn_y, btn_width, button_height},
        "Next >"
    };
    app.feed_settings_button = {
        {width - kOuterMargin - 176, 10, 176, header_height - 20},
        "Settings"
    };

    rewrite_framebuffer_clear(app.framebuffer, COLOR_WHITE);

    // Header
    const rewrite_rect_t header_rect{0, 0, width, header_height};
    rewrite_framebuffer_fill_rect(app.framebuffer, header_rect, kColorHeader);
    draw_button(app, app.feed_settings_button, kColorButtonSecondary);
    draw_header_brand(app, header_rect);

    // Posts
    const auto& items = app.feed_result.feed.items;
    const int total = static_cast<int>(items.size());
    int cursor_y = content_top + kOuterMargin;
    int rendered = 0;
    app.visible_post_hits.clear();

    if (app.feed_result.state != rewrite_feed_state_t::loaded || total == 0) {
        const char* msg = total == 0 && app.feed_result.state == rewrite_feed_state_t::loaded
            ? "No posts in timeline"
            : app.feed_result.error_message.c_str();
        font_draw_wrapped(&app.text_fb,
                          kOuterMargin, cursor_y, content_width,
                          msg, COLOR_BLACK, COLOR_WHITE, line_spacing);
    } else {
        for (int i = app.feed_page_start; i < total && cursor_y < content_bottom; ++i) {
            const Bsky::Post& post = items[i];
            const int post_top = cursor_y;
            // Pre-measure to see if this post will fit
            int needed = 0;
            const std::string hidden_message = hidden_post_message(app, post);
            if (!post.reposted_by.empty()) {
                needed += app.text_fb.font_h + 2;
            }
            needed += std::max(app.text_fb.font_h + 4, app.profile_images_enabled ? kFeedAvatarSize + 4 : 0); // author/avatar row
            if (!hidden_message.empty()) {
                needed += font_measure_wrapped(post_text_width - 8, hidden_message.c_str(), line_spacing);
                needed += 4;
            } else {
                const std::string measured_post_text = sanitize_bitmap_text(post.text);
                needed += font_measure_wrapped(post_text_width - 8, measured_post_text.c_str(), line_spacing);
                needed += measure_embed_block_height(app, post, post_text_width - 8);
            }
            needed += app.text_fb.font_h + kPostSeparator + 8; // stats + separator

            // Skip this post if it would overflow (except the first one — always show at least 1)
            if (rendered > 0 && cursor_y + needed > content_bottom) break;

            // Repost attribution
            if (!post.reposted_by.empty()) {
                std::string repost_line = sanitize_bitmap_text(">> Reposted by @" + post.reposted_by);
                draw_text(app,
                          text_left,
                          cursor_y,
                          repost_line,
                          rewrite_text_role_t::meta,
                          kColorRepost,
                          COLOR_WHITE);
                cursor_y += app.text_fb.font_h + 2;
            }

            // Author line
            std::string author = post.author.display_name.empty()
                ? "@" + post.author.handle
                : post.author.display_name + "  @" + post.author.handle;
            author = sanitize_bitmap_text(author);

            char time_buf[64];
            str_format_time(time_buf, sizeof(time_buf), static_cast<long>(post.indexed_at));

            const int author_y = cursor_y;
            if (app.profile_images_enabled) {
                const int avatar_x = kOuterMargin;
                if (!post.author.avatar_url.empty()) {
                    const image_t* avatar = image_cache_lookup(post.author.avatar_url.c_str(),
                                                               kFeedAvatarSize,
                                                               kFeedAvatarSize);
                    if (avatar) {
                        fb_blit_rgba(&app.text_fb,
                                     avatar_x,
                                     author_y,
                                     avatar->width,
                                     avatar->height,
                                     avatar->pixels);
                    } else {
                        fb_fill_rect(&app.text_fb,
                                     avatar_x,
                                     author_y,
                                     kFeedAvatarSize,
                                     kFeedAvatarSize,
                                     COLOR_LGRAY);
                    }
                } else {
                    fb_fill_rect(&app.text_fb,
                                 avatar_x,
                                 author_y,
                                 kFeedAvatarSize,
                                 kFeedAvatarSize,
                                 COLOR_LGRAY);
                }
            }

            draw_text(app,
                      text_left,
                      cursor_y,
                      author,
                      rewrite_text_role_t::author,
                      kColorAuthor,
                      COLOR_WHITE);
            // Draw timestamp right-aligned
            {
                int time_w = measure_text(time_buf, rewrite_text_role_t::meta);
                int time_x = width - kOuterMargin - time_w;
                if (time_x > text_left + measure_text(author, rewrite_text_role_t::author) + 8) {
                    draw_text(app,
                              time_x,
                              cursor_y,
                              time_buf,
                              rewrite_text_role_t::meta,
                              kColorMeta,
                              COLOR_WHITE);
                }
            }
            cursor_y += std::max(app.text_fb.font_h + 4,
                                 app.profile_images_enabled ? kFeedAvatarSize + 4 : 0);

            // Post text
            if (!hidden_message.empty()) {
                const int text_top = cursor_y;
                const int text_height = font_measure_wrapped(post_text_width - 8,
                                                             hidden_message.c_str(),
                                                             line_spacing);
                font_draw_wrapped(&app.text_fb,
                                  text_left + 4, cursor_y,
                                  post_text_width - 8,
                                  hidden_message.c_str(),
                                  kColorMeta, COLOR_WHITE,
                                  line_spacing);
                cursor_y = text_top + text_height;
                cursor_y += 4;
            } else if (!post.text.empty()) {
                const std::string post_text = sanitize_bitmap_text(post.text);
                const int text_top = cursor_y;
                const int text_height = font_measure_wrapped(post_text_width - 8,
                                                             post_text.c_str(),
                                                             line_spacing);
                font_draw_wrapped(&app.text_fb,
                                  text_left + 4, cursor_y,
                                  post_text_width - 8,
                                  post_text.c_str(),
                                  COLOR_BLACK, COLOR_WHITE,
                                  line_spacing);
                cursor_y = text_top + text_height;
                cursor_y += 4;
            }

            if (hidden_message.empty()) {
                draw_embed_block(app, post, text_left + 4, cursor_y, post_text_width - 8);
            }

            // Stats line
            const int stats_x = text_left + 4;
            const int stats_y = cursor_y;
            const std::string like_label = feed_like_label(post);
            const std::string reply_label = feed_reply_label(post);
            const std::string repost_label = feed_repost_label(post);
            const std::string stats = like_label + "  " + reply_label + "  " + repost_label;
            draw_text(app,
                      stats_x,
                      cursor_y,
                      stats,
                      rewrite_text_role_t::meta,
                      kColorMeta,
                      COLOR_WHITE);
            cursor_y += app.text_fb.font_h;

            const std::string middle_label = like_label + "  " + reply_label + "  ";
            const int like_width = std::max(72, font_measure_string(like_label.c_str()) + 12);
            const int repost_x = stats_x + font_measure_string(middle_label.c_str());
            const int repost_width = std::max(72, font_measure_string(repost_label.c_str()) + 16);

            // Separator line
            cursor_y += kPostSeparator / 2;
            rewrite_framebuffer_fill_rect(app.framebuffer,
                rewrite_rect_t{kOuterMargin, cursor_y, content_width, 1},
                kColorPostBorder);
            cursor_y += kPostSeparator / 2;

            app.visible_post_hits.push_back({
                rewrite_rect_t{kOuterMargin, post_top, content_width, cursor_y - post_top},
                make_stat_hit_rect(stats_x, stats_y, like_width, app.text_fb.font_h),
                make_stat_hit_rect(repost_x, stats_y, repost_width, app.text_fb.font_h),
                make_stat_hit_rect(stats_x, stats_y, font_measure_string(stats.c_str()) + 12, app.text_fb.font_h),
                stats_x,
                stats_y,
                i,
            });

            rendered++;
        }
    }
    app.feed_page_count = rendered;

    // Page indicator in a small status area above buttons
    {
        int end_idx = app.feed_page_start + rendered;
        const int first_idx = total == 0 ? 0 : app.feed_page_start + 1;
        std::string page_info = "Posts " + std::to_string(first_idx) +
                                "-" + std::to_string(end_idx) +
                                " of " + std::to_string(total);
        if (!app.feed_result.feed.cursor.empty()) page_info += "+";
        int info_y = btn_y - app.text_fb.font_h - 8;
        draw_centered_text(app, width / 2, info_y, page_info, kColorMeta, COLOR_WHITE, rewrite_text_role_t::meta);
    }

    // Buttons
    draw_button(app, app.feed_back_button, kColorButtonSecondary);
    draw_button(app, app.feed_refresh_button, kColorButtonSecondary);
    draw_button(app, app.feed_next_button, kColorButtonPrimary);

    std::string error_message;
    const rewrite_rect_t refresh_rect = full_refresh
        ? full_screen_rect(app)
        : rewrite_rect_t{0, header_height, width, btn_y - header_height};
    if (!rewrite_framebuffer_refresh(app.framebuffer,
                                     ui_refresh_mode(app, full_refresh),
                                     refresh_rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: feed refresh failed: %s\n", error_message.c_str());
    }
}

void render_thread_screen(rewrite_app_t& app, bool full_refresh) {
    const int width = app.framebuffer.info.screen_width;
    const int height = app.framebuffer.info.screen_height;
    const int header_height = std::max(72, height / 14);
    const int content_right = width - kOuterMargin - kThreadScrollbarWidth - kThreadScrollbarGap;
    const int content_width = content_right - kOuterMargin;
    const int content_bottom = height - kOuterMargin;
    const int line_spacing = 4;
    const int scrollbar_top = header_height + kOuterMargin;
    const int scrollbar_height = std::max(80, content_bottom - scrollbar_top);

    app.thread_back_button = {{kOuterMargin, 10, 160, header_height - 20}, "< Back"};
    app.thread_settings_button = {{width - kOuterMargin - 176, 10, 176, header_height - 20}, "Settings"};
    app.thread_scrollbar_rect = {width - kOuterMargin - kThreadScrollbarWidth,
                                 scrollbar_top,
                                 kThreadScrollbarWidth,
                                 scrollbar_height};
    app.thread_scrollbar_thumb_rect = app.thread_scrollbar_rect;
    app.thread_post_hits.clear();
    app.thread_page_count = 0;

    rewrite_framebuffer_clear(app.framebuffer, COLOR_WHITE);

    const rewrite_rect_t header_rect{0, 0, width, header_height};
    rewrite_framebuffer_fill_rect(app.framebuffer, header_rect, kColorHeader);
    draw_button(app, app.thread_back_button, kColorButtonSecondary);
    draw_button(app, app.thread_settings_button, kColorButtonSecondary);
    draw_header_brand(app, header_rect);

    int cursor_y = header_height + kOuterMargin;
    if (app.thread_result.state != rewrite_thread_state_t::loaded) {
        const char* msg = app.thread_result.error_message.empty()
            ? "Loading thread..."
            : app.thread_result.error_message.c_str();
        font_draw_wrapped(&app.text_fb,
                          kOuterMargin,
                          cursor_y,
                          content_width,
                          msg,
                          COLOR_BLACK,
                          COLOR_WHITE,
                          line_spacing);
    } else {
        std::vector<std::pair<const Bsky::Post*, int>> nodes;
        flatten_thread_posts(app.thread_result.root, 0, nodes);
        const int total = static_cast<int>(nodes.size());
        app.thread_page_start = clamp_thread_page_start(app.thread_page_start, total);
        int rendered = 0;
        for (int index = app.thread_page_start; index < total && cursor_y < content_bottom; ++index) {
            const auto& node = nodes[index];
            Bsky::Post* post = const_cast<Bsky::Post*>(node.first);
            const int depth = node.second;
            const int indent = depth * 28;
            const int avatar_block_width = app.profile_images_enabled ? (kThreadAvatarSize + 8) : 0;
            const int x = kOuterMargin + indent;
            const int text_x = x + avatar_block_width;
            const int width_for_post = std::max(120, content_width - indent);
            const int text_width_for_post = std::max(120, width_for_post - avatar_block_width);
            const int post_top = cursor_y;
            const int needed = measure_thread_post_height(app,
                                                          *post,
                                                          depth > 0,
                                                          text_width_for_post);

            if (rendered > 0 && cursor_y + needed > content_bottom) {
                break;
            }

            if (depth > 0) {
                rewrite_framebuffer_fill_rect(app.framebuffer,
                                              rewrite_rect_t{kOuterMargin + indent - 12, cursor_y, 2, app.text_fb.font_h + 8},
                                              kColorPostBorder);
            }

            std::string author = post->author.display_name.empty()
                ? "@" + post->author.handle
                : post->author.display_name + "  @" + post->author.handle;
            author = sanitize_bitmap_text(author);

            const int author_y = cursor_y;
            if (app.profile_images_enabled) {
                if (!post->author.avatar_url.empty()) {
                    const image_t* avatar = image_cache_lookup(post->author.avatar_url.c_str(),
                                                               kThreadAvatarSize,
                                                               kThreadAvatarSize);
                    if (avatar) {
                        fb_blit_rgba(&app.text_fb,
                                     x,
                                     author_y,
                                     avatar->width,
                                     avatar->height,
                                     avatar->pixels);
                    } else {
                        fb_fill_rect(&app.text_fb,
                                     x,
                                     author_y,
                                     kThreadAvatarSize,
                                     kThreadAvatarSize,
                                     COLOR_LGRAY);
                    }
                } else {
                    fb_fill_rect(&app.text_fb,
                                 x,
                                 author_y,
                                 kThreadAvatarSize,
                                 kThreadAvatarSize,
                                 COLOR_LGRAY);
                }
            }

            draw_text(app, text_x, cursor_y, author, rewrite_text_role_t::author, kColorAuthor, COLOR_WHITE);
            cursor_y += std::max(app.text_fb.font_h + 4,
                                 app.profile_images_enabled ? kThreadAvatarSize + 4 : 0);

            const std::string hidden_message = hidden_post_message(app, *post);
            if (!hidden_message.empty()) {
                const int text_top = cursor_y;
                const int text_height = font_measure_wrapped(text_width_for_post,
                                                             hidden_message.c_str(),
                                                             line_spacing);
                font_draw_wrapped(&app.text_fb,
                                  text_x,
                                  cursor_y,
                                  text_width_for_post,
                                  hidden_message.c_str(),
                                  kColorMeta,
                                  COLOR_WHITE,
                                  line_spacing);
                cursor_y = text_top + text_height;
                cursor_y += 4;
            } else {
                const std::string text = sanitize_bitmap_text(post->text);
                const int text_top = cursor_y;
                const int text_height = font_measure_wrapped(text_width_for_post,
                                                             text.c_str(),
                                                             line_spacing);
                font_draw_wrapped(&app.text_fb,
                                  text_x,
                                  cursor_y,
                                  text_width_for_post,
                                  text.c_str(),
                                  COLOR_BLACK,
                                  COLOR_WHITE,
                                  line_spacing);
                cursor_y = text_top + text_height;
                cursor_y += 4;

                draw_embed_block(app, *post, text_x, cursor_y, text_width_for_post);
            }

            const std::string like_label = thread_like_label(*post);
            const std::string reply_label = thread_reply_label(*post);
            const std::string repost_label = thread_repost_label(*post);
            const std::string stats = like_label + "  " + reply_label + "  " + repost_label;
            draw_text(app, text_x, cursor_y, stats, rewrite_text_role_t::meta, kColorMeta, COLOR_WHITE);
            const int stats_y = cursor_y;
            const int like_width = std::max(72, font_measure_string(like_label.c_str()) + 16);
            const std::string middle_label = like_label + "  " + reply_label + "  ";
            const int repost_x = text_x + font_measure_string(middle_label.c_str());
            const int repost_width = std::max(72, font_measure_string(repost_label.c_str()) + 16);
            cursor_y += app.text_fb.font_h + 8;

            rewrite_framebuffer_fill_rect(app.framebuffer,
                                          rewrite_rect_t{x, cursor_y, width_for_post, 1},
                                          kColorPostBorder);
            app.thread_post_hits.push_back({
                rewrite_rect_t{x, post_top, width_for_post, cursor_y - post_top + 1},
                make_stat_hit_rect(text_x, stats_y, like_width, app.text_fb.font_h),
                make_stat_hit_rect(repost_x, stats_y, repost_width, app.text_fb.font_h),
                make_stat_hit_rect(text_x, stats_y, font_measure_string(stats.c_str()) + 12, app.text_fb.font_h),
                text_x,
                stats_y,
                post,
            });
            cursor_y += 12;
            rendered++;

            if (cursor_y > height - kOuterMargin - app.text_fb.font_h) {
                break;
            }
        }

        app.thread_page_count = rendered;

        rewrite_framebuffer_fill_rect(app.framebuffer,
                                      app.thread_scrollbar_rect,
                                      0xDD);
        draw_border(app, app.thread_scrollbar_rect, kColorPostBorder, 1);

        if (total > 0 && rendered > 0) {
            const int max_start = std::max(0, total - rendered);
            const int thumb_height = std::max(40,
                                              (app.thread_scrollbar_rect.height * rendered) /
                                                  std::max(1, total));
            const int travel = std::max(0, app.thread_scrollbar_rect.height - thumb_height);
            int thumb_y = app.thread_scrollbar_rect.y;
            if (max_start > 0 && travel > 0) {
                thumb_y += (app.thread_page_start * travel) / max_start;
            }
            app.thread_scrollbar_thumb_rect = {app.thread_scrollbar_rect.x + 2,
                                               thumb_y + 2,
                                               std::max(4, app.thread_scrollbar_rect.width - 4),
                                               std::max(12, thumb_height - 4)};
            rewrite_framebuffer_fill_rect(app.framebuffer,
                                          app.thread_scrollbar_thumb_rect,
                                          kColorButtonPrimary);
        }
    }

    std::string error_message;
    const rewrite_rect_t refresh_rect = full_refresh
        ? full_screen_rect(app)
        : rewrite_rect_t{0, header_height, width, height - header_height};
    if (!rewrite_framebuffer_refresh(app.framebuffer,
                                     ui_refresh_mode(app, full_refresh),
                                     refresh_rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: thread refresh failed: %s\n", error_message.c_str());
    }
}

void draw_toggle_chip(rewrite_app_t& app, const rewrite_rect_t& rect, bool enabled) {
    const std::uint8_t fill = enabled ? kColorButtonPrimary : 0xC8;
    rewrite_framebuffer_fill_rect(app.framebuffer, rect, fill);
    draw_border(app, rect, COLOR_BLACK, 2);
    draw_centered_text(app,
                       rect.x + rect.width / 2,
                       rect.y + (rect.height - line_height(rewrite_text_role_t::button)) / 2,
                       enabled ? "ON" : "OFF",
                       enabled ? COLOR_WHITE : COLOR_BLACK,
                       fill,
                       rewrite_text_role_t::button);
}

void draw_settings_row(rewrite_app_t& app,
                       const rewrite_button_t& row,
                       const std::string& label,
                       const std::string& detail,
                       bool enabled) {
    rewrite_framebuffer_fill_rect(app.framebuffer, row.rect, kColorCard);
    draw_border(app, row.rect, kColorPostBorder, 2);

    const int text_x = row.rect.x + kCardPadding;
    const int text_y = row.rect.y + 12;
    draw_text(app, text_x, text_y, label, rewrite_text_role_t::settings_label, COLOR_BLACK, kColorCard);
    draw_wrapped_text(app,
                      text_x,
                      text_y + line_height(rewrite_text_role_t::settings_label) + 6,
                      row.rect.width - 180,
                      detail,
                      rewrite_text_role_t::settings_detail,
                      kColorMeta,
                      kColorCard);

    const rewrite_rect_t toggle_rect{row.rect.x + row.rect.width - 116,
                                     row.rect.y + (row.rect.height - 46) / 2,
                                     92,
                                     46};
    draw_toggle_chip(app, toggle_rect, enabled);
}

void draw_settings_action_row(rewrite_app_t& app,
                              const rewrite_button_t& row,
                              const std::string& label,
                              const std::string& detail,
                              const std::string& action_label) {
    rewrite_framebuffer_fill_rect(app.framebuffer, row.rect, kColorCard);
    draw_border(app, row.rect, kColorPostBorder, 2);

    const int text_x = row.rect.x + kCardPadding;
    const int text_y = row.rect.y + 12;
    draw_text(app, text_x, text_y, label, rewrite_text_role_t::settings_label, COLOR_BLACK, kColorCard);
    draw_wrapped_text(app,
                      text_x,
                      text_y + line_height(rewrite_text_role_t::settings_label) + 6,
                      row.rect.width - 180,
                      detail,
                      rewrite_text_role_t::settings_detail,
                      kColorMeta,
                      kColorCard);

    const rewrite_rect_t action_rect{row.rect.x + row.rect.width - 124,
                                     row.rect.y + (row.rect.height - 46) / 2,
                                     100,
                                     46};
    rewrite_framebuffer_fill_rect(app.framebuffer, action_rect, kColorButtonSecondary);
    draw_border(app, action_rect, COLOR_BLACK, 2);
    draw_centered_text(app,
                       action_rect.x + action_rect.width / 2,
                       action_rect.y + (action_rect.height - line_height(rewrite_text_role_t::action_label)) / 2,
                       action_label,
                       COLOR_BLACK,
                       kColorButtonSecondary,
                       rewrite_text_role_t::action_label);
}

void render_settings_screen(rewrite_app_t& app, bool full_refresh) {
    const int width = app.framebuffer.info.screen_width;
    const int height = app.framebuffer.info.screen_height;
    const int header_height = std::max(72, height / 14);
    const int row_height = std::max(104, height / 10);
    const int row_gap = 16;
    const int content_width = width - (kOuterMargin * 2);
    const int first_row_y = header_height + kOuterMargin;

    app.settings_back_button = {{kOuterMargin, 10, 160, header_height - 20}, "< Back"};
    app.settings_profile_button = {{kOuterMargin, first_row_y, content_width, row_height}, "Profile Images"};
    app.settings_embed_button = {{kOuterMargin, first_row_y + row_height + row_gap, content_width, row_height}, "Embed Images"};
    app.settings_nudity_button = {{kOuterMargin, first_row_y + (row_height + row_gap) * 2, content_width, row_height}, "Nudity"};
    app.settings_porn_button = {{kOuterMargin, first_row_y + (row_height + row_gap) * 3, content_width, row_height}, "Porn"};
    app.settings_suggestive_button = {{kOuterMargin, first_row_y + (row_height + row_gap) * 4, content_width, row_height}, "Suggestive"};
    app.settings_diagnostics_button = {{kOuterMargin, first_row_y + (row_height + row_gap) * 5, content_width, row_height}, "Diagnostics"};
    app.settings_sign_out_button = {{kOuterMargin,
                                     height - kOuterMargin - std::max(90, height / 12),
                                     content_width,
                                     std::max(90, height / 12)},
                                    "Sign Out"};

    rewrite_framebuffer_clear(app.framebuffer, COLOR_WHITE);

    const rewrite_rect_t header_rect{0, 0, width, header_height};
    rewrite_framebuffer_fill_rect(app.framebuffer, header_rect, kColorHeader);
    draw_button(app, app.settings_back_button, kColorButtonSecondary);
    draw_header_brand(app, header_rect);

    draw_settings_row(app,
                      app.settings_profile_button,
                      "Profile Images",
                      "Load avatar images when image rendering is available.",
                      app.profile_images_enabled);
    draw_settings_row(app,
                      app.settings_embed_button,
                      "Embed Images",
                      "Show post image placeholders and enable image loading later.",
                      app.embed_images_enabled);
    draw_settings_row(app,
                      app.settings_nudity_button,
                      "Nudity",
                      "Show posts labeled for nudity.",
                      app.allow_nudity_content);
    draw_settings_row(app,
                      app.settings_porn_button,
                      "Porn",
                      "Show posts labeled for pornographic content.",
                      app.allow_porn_content);
    draw_settings_row(app,
                      app.settings_suggestive_button,
                      "Suggestive",
                      "Show posts labeled for suggestive or sexual content.",
                      app.allow_suggestive_content);
    draw_settings_action_row(app,
                             app.settings_diagnostics_button,
                             "Diagnostics",
                             "Open the device and authentication diagnostics screen that used to appear at startup.",
                             "Open");

    draw_wrapped_text(app,
                      kOuterMargin,
                      app.settings_diagnostics_button.rect.y + app.settings_diagnostics_button.rect.height + 20,
                      content_width,
                      "These toggles are persisted. Adult-content categories default to hidden until enabled here.",
                      rewrite_text_role_t::settings_detail,
                      kColorMeta,
                      COLOR_WHITE);

    draw_button(app, app.settings_sign_out_button, kColorButtonPrimary);

    std::string error_message;
    const rewrite_rect_t screen_rect = full_screen_rect(app);
    if (!rewrite_framebuffer_refresh(app.framebuffer,
                                     ui_refresh_mode(app, full_refresh),
                                     screen_rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: settings refresh failed: %s\n", error_message.c_str());
    }
}

void render_fatal_screen(rewrite_framebuffer_t& framebuffer, fb_t& text_fb, const std::string& message) {
    rewrite_framebuffer_clear(framebuffer, COLOR_WHITE);
    const rewrite_rect_t box{48, 120, framebuffer.info.screen_width - 96, framebuffer.info.screen_height - 240};
    rewrite_framebuffer_fill_rect(framebuffer, box, 0xF0);

    auto draw_box_border = [&](const rewrite_rect_t& rect) {
        rewrite_framebuffer_fill_rect(framebuffer, rewrite_rect_t{rect.x, rect.y, rect.width, 3}, COLOR_BLACK);
        rewrite_framebuffer_fill_rect(framebuffer, rewrite_rect_t{rect.x, rect.y + rect.height - 3, rect.width, 3}, COLOR_BLACK);
        rewrite_framebuffer_fill_rect(framebuffer, rewrite_rect_t{rect.x, rect.y, 3, rect.height}, COLOR_BLACK);
        rewrite_framebuffer_fill_rect(framebuffer, rewrite_rect_t{rect.x + rect.width - 3, rect.y, 3, rect.height}, COLOR_BLACK);
    };

    draw_box_border(box);
    font_draw_string(&text_fb, box.x + 20, box.y + 20, "sKeets startup failed", COLOR_BLACK, 0xF0);
    font_draw_wrapped(&text_fb,
                      box.x + 20,
                      box.y + 20 + text_fb.font_h + 16,
                      box.width - 40,
                      message.c_str(),
                      COLOR_BLACK,
                      0xF0,
                      4);

    std::string error_message;
    const rewrite_rect_t screen_rect{0, 0, framebuffer.info.screen_width, framebuffer.info.screen_height};
    if (!rewrite_framebuffer_refresh(framebuffer,
                                     rewrite_refresh_mode_t::full,
                                     screen_rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: fatal-screen refresh failed: %s\n", error_message.c_str());
    }
}

void render_active_view(rewrite_app_t& app, bool full_refresh) {
    if (app.view_mode == rewrite_view_mode_t::feed) {
        render_feed_screen(app, full_refresh);
        return;
    }
    if (app.view_mode == rewrite_view_mode_t::thread) {
        render_thread_screen(app, full_refresh);
        return;
    }
    if (app.view_mode == rewrite_view_mode_t::settings) {
        render_settings_screen(app, full_refresh);
        return;
    }
    if (app.view_mode == rewrite_view_mode_t::diagnostics) {
        render_diagnostics_screen(app, full_refresh);
        return;
    }
    render_screen(app, full_refresh);
}

} // namespace

int main(int argc, char* argv[]) {
    // Force-load our bundled OpenSSL 3.x before Qt initializes TLS.
    // Qt's TLS plugin uses dlopen() at runtime and on Kobo devices it
    // picks up the system's ancient OpenSSL 1.0.x instead of our bundled
    // 3.x, even with LD_LIBRARY_PATH set correctly.  Loading here with
    // RTLD_GLOBAL makes the symbols available process-wide so the TLS
    // plugin finds them already resident.
    {
        const std::string lib_dir = rewrite_dir() + "/lib";
        const std::string crypto_path = lib_dir + "/libcrypto.so.3";
        const std::string ssl_path = lib_dir + "/libssl.so.3";

        void* crypto = dlopen(crypto_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!crypto) {
            std::fprintf(stderr, "rewrite openssl: dlopen(%s) failed: %s\n",
                         crypto_path.c_str(), dlerror());
        }

        void* ssl = dlopen(ssl_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!ssl) {
            std::fprintf(stderr, "rewrite openssl: dlopen(%s) failed: %s\n",
                         ssl_path.c_str(), dlerror());
        }
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("sKeets-rewrite");

    // Load our bundled CA certificate bundle so Qt can verify TLS chains.
    // Qt on embedded Linux doesn't check SSL_CERT_FILE — it only looks at
    // system paths like /etc/ssl/certs which may be outdated or absent on
    // the Kobo.  Set the default SSL configuration explicitly.
    {
        const QString ca_path = QString::fromStdString(rewrite_dir() + "/ssl/certs/ca-certificates.crt");
        QFile ca_file(ca_path);
        if (ca_file.open(QIODevice::ReadOnly)) {
            auto certs = QSslCertificate::fromDevice(&ca_file, QSsl::Pem);
            if (!certs.isEmpty()) {
                auto config = QSslConfiguration::defaultConfiguration();
                config.setCaCertificates(certs);
                QSslConfiguration::setDefaultConfiguration(config);
            } else {
                std::fprintf(stderr, "rewrite ssl: no certs parsed from %s\n",
                             ca_path.toUtf8().constData());
            }
        } else {
            std::fprintf(stderr, "rewrite ssl: failed to open %s\n",
                         ca_path.toUtf8().constData());
        }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGHUP, handle_signal);

    rewrite_app_t rewrite_app;
    rewrite_app.device = rewrite_probe_device();
    rewrite_app.revision_summary = read_revision_summary();

    std::string error_message;
    if (!rewrite_framebuffer_open(rewrite_app.framebuffer, &error_message)) {
        std::fprintf(stderr, "rewrite app: framebuffer open failed: %s\n", error_message.c_str());
        return 1;
    }

    rewrite_app.text_fb.fd = rewrite_app.framebuffer.fd;
    rewrite_app.text_fb.width = rewrite_app.framebuffer.info.screen_width;
    rewrite_app.text_fb.height = rewrite_app.framebuffer.info.screen_height;
    rewrite_app.text_fb.font_w = rewrite_app.framebuffer.info.font_width;
    rewrite_app.text_fb.font_h = rewrite_app.framebuffer.info.font_height;
    if (fb_load_fonts(&rewrite_app.text_fb, (rewrite_dir() + "/fonts").c_str()) == 0) {
        font_set_ot_enabled(true);
    } else {
        font_set_ot_enabled(false);
    }
    font_init(&rewrite_app.text_fb);
    load_brand_assets(rewrite_app);

    if (!rewrite_input_open(rewrite_app.input,
                            rewrite_app.framebuffer.info.screen_width,
                            rewrite_app.framebuffer.info.screen_height,
                            detect_input_protocol(),
                            &error_message)) {
        std::fprintf(stderr, "rewrite app: input open failed: %s\n", error_message.c_str());
        render_fatal_screen(rewrite_app.framebuffer, rewrite_app.text_fb, error_message);
        rewrite_framebuffer_close(rewrite_app.framebuffer);
        return 1;
    }

    refresh_bootstrap_state(rewrite_app);
    refresh_runtime_state(rewrite_app);
    load_settings(rewrite_app);

    // If auth succeeded, auto-load the feed and switch to feed view
    if (rewrite_app.bootstrap.authenticated) {
        render_loading_screen(rewrite_app,
                              "Loading timeline",
                              "Fetching your home feed and warming the first image batch.",
                              true);
        load_feed(rewrite_app);
        if (rewrite_app.feed_result.state == rewrite_feed_state_t::loaded) {
            queue_feed_image_requests(rewrite_app);
            render_loading_screen(rewrite_app,
                                  "Loading media",
                                  "Starting avatar and preview downloads for the first visible posts.",
                                  true);
            wait_for_initial_feed_images(1200);
            rewrite_app.view_mode = rewrite_view_mode_t::feed;
            render_feed_screen(rewrite_app, true);
        } else {
            render_screen(rewrite_app, true);
        }
    } else {
        render_screen(rewrite_app, true);
    }

    auto last_probe = std::chrono::steady_clock::now();
    while (!g_stop_requested) {
        QCoreApplication::processEvents();

        rewrite_input_event_t event;
        error_message.clear();
        const bool have_event = rewrite_input_poll(rewrite_app.input, event, 500, &error_message);
        if (!have_event) {
            if (image_cache_redraw_needed() &&
                (rewrite_app.view_mode == rewrite_view_mode_t::feed ||
                 rewrite_app.view_mode == rewrite_view_mode_t::thread)) {
                render_active_view(rewrite_app, false);
            }

            if (!error_message.empty()) {
                rewrite_app.status_line = "Input polling error";
                rewrite_app.input_line = error_message;
                render_active_view(rewrite_app, false);
            }

            const auto now = std::chrono::steady_clock::now();
            if (now - last_probe >= std::chrono::seconds(20)) {
                refresh_runtime_state(rewrite_app);
                if (rewrite_app.view_mode != rewrite_view_mode_t::feed) {
                    rewrite_app.status_line = rewrite_app.bootstrap.headline;
                    rewrite_app.input_line = rewrite_app.bootstrap.detail;
                }
                render_active_view(rewrite_app, false);
                last_probe = now;
            }
            continue;
        }

        if (event.type == rewrite_input_event_type_t::key_press) {
            if (event.key_code == KEY_POWER) {
                rewrite_app.status_line = "Power key pressed";
                rewrite_app.input_line = "Exiting rewrite auth bootstrap shell";
                render_screen(rewrite_app, false);
                break;
            }

            rewrite_app.status_line = "Unhandled key press";
            rewrite_app.input_line = "Linux input code " + std::to_string(event.key_code);
            render_screen(rewrite_app, false);
            continue;
        }

        if (event.type != rewrite_input_event_type_t::touch_up) {
            continue;
        }

        std::ostringstream touch_message;
        touch_message << "Touch released at " << event.x << "," << event.y;

        // --- Feed view touch handling ---
        if (rewrite_app.view_mode == rewrite_view_mode_t::feed) {
            if (contains_point(rewrite_app.feed_settings_button.rect, event.x, event.y)) {
                open_settings(rewrite_app, rewrite_view_mode_t::feed);
                render_settings_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.feed_back_button.rect, event.x, event.y)) {
                if (!rewrite_app.feed_page_history.empty()) {
                    rewrite_app.feed_page_start = rewrite_app.feed_page_history.back();
                    rewrite_app.feed_page_history.pop_back();
                    render_feed_screen(rewrite_app, true);
                } else {
                    rewrite_app.status_line = "Already at newest page";
                    rewrite_app.input_line = touch_message.str();
                    render_feed_screen(rewrite_app, false);
                }
                continue;
            }

            if (contains_point(rewrite_app.feed_refresh_button.rect, event.x, event.y)) {
                render_feed_screen(rewrite_app, false);  // Show current state
                load_feed(rewrite_app);
                render_feed_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.feed_next_button.rect, event.x, event.y)) {
                if (advance_feed_page(rewrite_app)) {
                    render_feed_screen(rewrite_app, true);
                } else {
                    rewrite_app.status_line = "Already at oldest loaded feed page";
                    rewrite_app.input_line = touch_message.str();
                    render_feed_screen(rewrite_app, false);
                }
                continue;
            }

            for (const auto& hit : rewrite_app.visible_post_hits) {
                if (!contains_point(hit.rect, event.x, event.y)) {
                    continue;
                }
                if (hit.post_index < 0 || hit.post_index >= static_cast<int>(rewrite_app.feed_result.feed.items.size())) {
                    continue;
                }
                Bsky::Post& post = rewrite_app.feed_result.feed.items[hit.post_index];

                if (contains_point(hit.like_rect, event.x, event.y)) {
                    if (post.viewer_like.empty()) {
                        post.like_count++;
                        post.viewer_like = "pending-like";
                        render_feed_stats_only(rewrite_app, hit);
                        const auto action = rewrite_like_post(rewrite_app.bootstrap.session, post.uri, post.cid);
                        apply_updated_session(rewrite_app, action.session, action.session_updated);
                        if (action.ok) {
                            post.viewer_like = action.record_uri;
                        } else {
                            post.viewer_like.clear();
                            post.like_count--;
                            std::fprintf(stderr, "rewrite action: like failed: %s\n", action.error_message.c_str());
                        }
                    } else {
                        const std::string old_uri = post.viewer_like;
                        post.viewer_like.clear();
                        post.like_count = std::max(0, post.like_count - 1);
                        render_feed_stats_only(rewrite_app, hit);
                        const auto action = rewrite_unlike_post(rewrite_app.bootstrap.session, old_uri);
                        apply_updated_session(rewrite_app, action.session, action.session_updated);
                        if (action.ok) {
                        } else {
                            post.viewer_like = old_uri;
                            post.like_count++;
                            std::fprintf(stderr, "rewrite action: unlike failed: %s\n", action.error_message.c_str());
                        }
                    }
                    render_feed_stats_only(rewrite_app, hit);
                    break;
                }

                if (contains_point(hit.repost_rect, event.x, event.y)) {
                    if (post.viewer_repost.empty()) {
                        post.repost_count++;
                        post.viewer_repost = "pending-repost";
                        render_feed_stats_only(rewrite_app, hit);
                        const auto action = rewrite_repost_post(rewrite_app.bootstrap.session, post.uri, post.cid);
                        apply_updated_session(rewrite_app, action.session, action.session_updated);
                        if (action.ok) {
                            post.viewer_repost = action.record_uri;
                        } else {
                            post.viewer_repost.clear();
                            post.repost_count--;
                            std::fprintf(stderr, "rewrite action: repost failed: %s\n", action.error_message.c_str());
                        }
                    } else {
                        const std::string old_uri = post.viewer_repost;
                        post.viewer_repost.clear();
                        post.repost_count = std::max(0, post.repost_count - 1);
                        render_feed_stats_only(rewrite_app, hit);
                        const auto action = rewrite_unrepost_post(rewrite_app.bootstrap.session, old_uri);
                        apply_updated_session(rewrite_app, action.session, action.session_updated);
                        if (action.ok) {
                        } else {
                            post.viewer_repost = old_uri;
                            post.repost_count++;
                            std::fprintf(stderr, "rewrite action: unrepost failed: %s\n", action.error_message.c_str());
                        }
                    }
                    render_feed_stats_only(rewrite_app, hit);
                    break;
                }

                load_thread(rewrite_app, post.uri);
                rewrite_app.view_mode = rewrite_view_mode_t::thread;
                render_thread_screen(rewrite_app, true);
                break;
            }

            // Tap anywhere else in feed — no action for now
            continue;
        }

        if (rewrite_app.view_mode == rewrite_view_mode_t::thread) {
            if (contains_point(rewrite_app.thread_back_button.rect, event.x, event.y)) {
                rewrite_app.view_mode = rewrite_view_mode_t::feed;
                render_feed_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.thread_settings_button.rect, event.x, event.y)) {
                open_settings(rewrite_app, rewrite_view_mode_t::thread);
                render_settings_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.thread_scrollbar_rect, event.x, event.y)) {
                std::vector<std::pair<const Bsky::Post*, int>> nodes;
                if (rewrite_app.thread_result.state == rewrite_thread_state_t::loaded) {
                    flatten_thread_posts(rewrite_app.thread_result.root, 0, nodes);
                }
                const int total = static_cast<int>(nodes.size());
                const int visible = std::max(1, rewrite_app.thread_page_count);
                const int max_start = std::max(0, total - visible);
                if (max_start > 0 && rewrite_app.thread_scrollbar_rect.height > 1) {
                    const int relative_y = std::max(0,
                                                    std::min(event.y - rewrite_app.thread_scrollbar_rect.y,
                                                             rewrite_app.thread_scrollbar_rect.height - 1));
                    rewrite_app.thread_page_start = (relative_y * max_start) /
                                                    std::max(1, rewrite_app.thread_scrollbar_rect.height - 1);
                    render_thread_screen(rewrite_app, true);
                } else {
                    rewrite_app.status_line = "Thread already fits on-screen";
                    rewrite_app.input_line = touch_message.str();
                    render_thread_screen(rewrite_app, false);
                }
                continue;
            }

            for (const auto& hit : rewrite_app.thread_post_hits) {
                if (!hit.post) {
                    continue;
                }

                Bsky::Post& post = *hit.post;
                if (contains_point(hit.like_rect, event.x, event.y)) {
                    if (post.viewer_like.empty()) {
                        post.like_count++;
                        post.viewer_like = "pending-like";
                        render_thread_stats_only(rewrite_app, hit);
                        const auto action = rewrite_like_post(rewrite_app.bootstrap.session, post.uri, post.cid);
                        apply_updated_session(rewrite_app, action.session, action.session_updated);
                        if (action.ok) {
                            post.viewer_like = action.record_uri;
                        } else {
                            post.viewer_like.clear();
                            post.like_count--;
                            std::fprintf(stderr, "rewrite action: thread like failed: %s\n", action.error_message.c_str());
                        }
                    } else {
                        const std::string old_uri = post.viewer_like;
                        post.viewer_like.clear();
                        post.like_count = std::max(0, post.like_count - 1);
                        render_thread_stats_only(rewrite_app, hit);
                        const auto action = rewrite_unlike_post(rewrite_app.bootstrap.session, old_uri);
                        apply_updated_session(rewrite_app, action.session, action.session_updated);
                        if (action.ok) {
                        } else {
                            post.viewer_like = old_uri;
                            post.like_count++;
                            std::fprintf(stderr, "rewrite action: thread unlike failed: %s\n", action.error_message.c_str());
                        }
                    }
                    render_thread_stats_only(rewrite_app, hit);
                    break;
                }

                if (contains_point(hit.repost_rect, event.x, event.y)) {
                    if (post.viewer_repost.empty()) {
                        post.repost_count++;
                        post.viewer_repost = "pending-repost";
                        render_thread_stats_only(rewrite_app, hit);
                        const auto action = rewrite_repost_post(rewrite_app.bootstrap.session, post.uri, post.cid);
                        apply_updated_session(rewrite_app, action.session, action.session_updated);
                        if (action.ok) {
                            post.viewer_repost = action.record_uri;
                        } else {
                            post.viewer_repost.clear();
                            post.repost_count--;
                            std::fprintf(stderr, "rewrite action: thread repost failed: %s\n", action.error_message.c_str());
                        }
                    } else {
                        const std::string old_uri = post.viewer_repost;
                        post.viewer_repost.clear();
                        post.repost_count = std::max(0, post.repost_count - 1);
                        render_thread_stats_only(rewrite_app, hit);
                        const auto action = rewrite_unrepost_post(rewrite_app.bootstrap.session, old_uri);
                        apply_updated_session(rewrite_app, action.session, action.session_updated);
                        if (action.ok) {
                        } else {
                            post.viewer_repost = old_uri;
                            post.repost_count++;
                            std::fprintf(stderr, "rewrite action: thread unrepost failed: %s\n", action.error_message.c_str());
                        }
                    }
                    render_thread_stats_only(rewrite_app, hit);
                    break;
                }

            }
            continue;
        }

        if (rewrite_app.view_mode == rewrite_view_mode_t::settings) {
            if (contains_point(rewrite_app.settings_back_button.rect, event.x, event.y)) {
                rewrite_app.view_mode = rewrite_app.settings_return_view;
                render_active_view(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.settings_profile_button.rect, event.x, event.y)) {
                rewrite_app.profile_images_enabled = !rewrite_app.profile_images_enabled;
                save_settings(rewrite_app);
                rewrite_app.status_line = "Settings updated";
                rewrite_app.input_line = std::string("Profile images ") +
                                         (rewrite_app.profile_images_enabled ? "enabled" : "disabled");
                render_settings_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.settings_embed_button.rect, event.x, event.y)) {
                rewrite_app.embed_images_enabled = !rewrite_app.embed_images_enabled;
                save_settings(rewrite_app);
                rewrite_app.status_line = "Settings updated";
                rewrite_app.input_line = std::string("Embed images ") +
                                         (rewrite_app.embed_images_enabled ? "enabled" : "disabled");
                render_settings_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.settings_nudity_button.rect, event.x, event.y)) {
                rewrite_app.allow_nudity_content = !rewrite_app.allow_nudity_content;
                save_settings(rewrite_app);
                rewrite_app.status_line = "Settings updated";
                rewrite_app.input_line = std::string("Nudity content ") +
                                         (rewrite_app.allow_nudity_content ? "shown" : "hidden");
                render_settings_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.settings_porn_button.rect, event.x, event.y)) {
                rewrite_app.allow_porn_content = !rewrite_app.allow_porn_content;
                save_settings(rewrite_app);
                rewrite_app.status_line = "Settings updated";
                rewrite_app.input_line = std::string("Porn content ") +
                                         (rewrite_app.allow_porn_content ? "shown" : "hidden");
                render_settings_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.settings_suggestive_button.rect, event.x, event.y)) {
                rewrite_app.allow_suggestive_content = !rewrite_app.allow_suggestive_content;
                save_settings(rewrite_app);
                rewrite_app.status_line = "Settings updated";
                rewrite_app.input_line = std::string("Suggestive content ") +
                                         (rewrite_app.allow_suggestive_content ? "shown" : "hidden");
                render_settings_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.settings_diagnostics_button.rect, event.x, event.y)) {
                rewrite_app.view_mode = rewrite_view_mode_t::diagnostics;
                rewrite_app.status_line = "Diagnostics";
                rewrite_app.input_line = "Use Refresh to re-probe device, network, and auth state.";
                render_diagnostics_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.settings_sign_out_button.rect, event.x, event.y)) {
                clear_saved_session(rewrite_app);
                refresh_bootstrap_state(rewrite_app);
                refresh_runtime_state(rewrite_app);
                rewrite_app.view_mode = rewrite_view_mode_t::dashboard;
                rewrite_app.status_line = "Signed out";
                rewrite_app.input_line = "Saved session cleared. Use login.txt or Recheck to sign in again.";
                render_screen(rewrite_app, true);
                continue;
            }

            continue;
        }

        if (rewrite_app.view_mode == rewrite_view_mode_t::diagnostics) {
            if (contains_point(rewrite_app.diagnostics_back_button.rect, event.x, event.y)) {
                rewrite_app.view_mode = rewrite_view_mode_t::settings;
                render_settings_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.diagnostics_refresh_button.rect, event.x, event.y)) {
                refresh_bootstrap_state(rewrite_app);
                refresh_runtime_state(rewrite_app);
                rewrite_app.status_line = "Diagnostics refreshed";
                rewrite_app.input_line = "Current authentication, device, power, and network state reloaded.";
                render_diagnostics_screen(rewrite_app, true);
                continue;
            }

            continue;
        }

        // --- Dashboard view touch handling ---

        if (contains_point(rewrite_app.exit_button.rect, event.x, event.y)) {
            rewrite_app.status_line = "Exit requested";
            rewrite_app.input_line = touch_message.str();
            render_screen(rewrite_app, false);
            break;
        }

        if (contains_point(rewrite_app.refresh_button.rect, event.x, event.y)) {
            refresh_bootstrap_state(rewrite_app);
            refresh_runtime_state(rewrite_app);
            if (rewrite_app.bootstrap.authenticated) {
                render_screen(rewrite_app, false);
                load_feed(rewrite_app);
                if (rewrite_app.feed_result.state == rewrite_feed_state_t::loaded) {
                    rewrite_app.view_mode = rewrite_view_mode_t::feed;
                    render_feed_screen(rewrite_app, true);
                } else {
                    render_screen(rewrite_app, false);
                }
            } else {
                rewrite_app.status_line = rewrite_app.bootstrap.headline;
                rewrite_app.input_line = touch_message.str();
                render_screen(rewrite_app, false);
            }
            last_probe = std::chrono::steady_clock::now();
            continue;
        }

        rewrite_app.status_line = "Touch received outside action buttons";
        rewrite_app.input_line = touch_message.str();
        render_screen(rewrite_app, false);
    }

    rewrite_input_close(rewrite_app.input);
    free_brand_assets(rewrite_app);
    rewrite_framebuffer_close(rewrite_app.framebuffer);
    return 0;
}