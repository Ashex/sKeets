#include "rewrite/bootstrap.h"
#include "rewrite/feed.h"
#include "rewrite/platform/device.h"
#include "rewrite/platform/framebuffer.h"
#include "rewrite/platform/input.h"
#include "rewrite/platform/network.h"
#include "rewrite/platform/power.h"
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
constexpr std::uint8_t kColorPostBorder = 0xC0;
constexpr std::uint8_t kColorAuthor = 0x10;
constexpr std::uint8_t kColorMeta = 0x60;
constexpr std::uint8_t kColorRepost = 0x50;

enum class rewrite_view_mode_t {
    dashboard,
    feed,
};

struct rewrite_button_t {
    rewrite_rect_t rect;
    std::string label;
};

struct rewrite_app_t {
    rewrite_framebuffer_t framebuffer;
    rewrite_input_t input;
    fb_t text_fb = {};
    rewrite_bootstrap_result_t bootstrap;
    rewrite_device_info_t device;
    rewrite_power_info_t power;
    rewrite_network_info_t network;
    std::string revision_summary;
    std::string status_line = "Checking rewrite auth bootstrap";
    std::string input_line = "Exit via the on-screen button or the hardware power key";
    rewrite_button_t refresh_button;
    rewrite_button_t exit_button;

    // Feed view state
    rewrite_view_mode_t view_mode = rewrite_view_mode_t::dashboard;
    rewrite_feed_result_t feed_result;
    int feed_page_start = 0;      // Index of first visible post
    int feed_page_count = 0;      // Number of posts that fit on last render
    rewrite_button_t feed_back_button;
    rewrite_button_t feed_refresh_button;
    rewrite_button_t feed_next_button;
};

void handle_signal(int) {
    g_stop_requested = 1;
}

std::string bool_label(bool value) {
    return value ? "yes" : "no";
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

std::string rewrite_dir() {
    const char* value = std::getenv("SKEETS_REWRITE_DIR");
    return value && *value ? value : "/mnt/onboard/.adds/sKeets-rewrite";
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

void draw_border(rewrite_app_t& app, const rewrite_rect_t& rect, std::uint8_t color, int thickness = 2) {
    rewrite_framebuffer_fill_rect(app.framebuffer, rewrite_rect_t{rect.x, rect.y, rect.width, thickness}, color);
    rewrite_framebuffer_fill_rect(app.framebuffer, rewrite_rect_t{rect.x, rect.y + rect.height - thickness, rect.width, thickness}, color);
    rewrite_framebuffer_fill_rect(app.framebuffer, rewrite_rect_t{rect.x, rect.y, thickness, rect.height}, color);
    rewrite_framebuffer_fill_rect(app.framebuffer, rewrite_rect_t{rect.x + rect.width - thickness, rect.y, thickness, rect.height}, color);
}

void draw_centered_text(rewrite_app_t& app, int center_x, int y, const std::string& text, std::uint8_t fg, std::uint8_t bg) {
    const int text_width = font_measure_string(text.c_str());
    const int start_x = std::max(kOuterMargin, center_x - text_width / 2);
    font_draw_string(&app.text_fb, start_x, y, text.c_str(), fg, bg);
}

void draw_card(rewrite_app_t& app,
               const rewrite_rect_t& rect,
               const std::string& title,
               const std::vector<std::string>& lines) {
    rewrite_framebuffer_fill_rect(app.framebuffer, rect, kColorCard);
    draw_border(app, rect, kColorCardBorder);

    const rewrite_rect_t title_rect{rect.x, rect.y, rect.width, app.text_fb.font_h + kCardPadding};
    rewrite_framebuffer_fill_rect(app.framebuffer, title_rect, kColorCardTitle);
    font_draw_string(&app.text_fb,
                     rect.x + kCardPadding,
                     rect.y + kCardPadding / 2,
                     title.c_str(),
                     COLOR_WHITE,
                     kColorCardTitle);

    int cursor_y = rect.y + title_rect.height + 12;
    const int max_width = rect.width - (kCardPadding * 2);
    for (const std::string& line : lines) {
        cursor_y = font_draw_wrapped(&app.text_fb,
                                     rect.x + kCardPadding,
                                     cursor_y,
                                     max_width,
                                     line.c_str(),
                                     COLOR_BLACK,
                                     kColorCard,
                                     4);
        cursor_y += 8;
        if (cursor_y >= rect.y + rect.height - app.text_fb.font_h - kCardPadding) {
            break;
        }
    }
}

void draw_button(rewrite_app_t& app, const rewrite_button_t& button, std::uint8_t fill) {
    rewrite_framebuffer_fill_rect(app.framebuffer, button.rect, fill);
    draw_border(app, button.rect, COLOR_BLACK, 3);

    const int center_y = button.rect.y + (button.rect.height - app.text_fb.font_h) / 2;
    draw_centered_text(app,
                       button.rect.x + button.rect.width / 2,
                       center_y,
                       button.label,
                       fill <= 0x40 ? COLOR_WHITE : COLOR_BLACK,
                       fill);
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

void load_feed(rewrite_app_t& app) {
    if (!app.bootstrap.authenticated) return;
    app.status_line = "Loading timeline...";
    app.feed_result = rewrite_fetch_feed(app.bootstrap.session);
    app.feed_page_start = 0;
    app.feed_page_count = 0;
    if (app.feed_result.state == rewrite_feed_state_t::loaded) {
        app.status_line = std::to_string(app.feed_result.post_count) + " posts loaded";
        app.input_line = "Tap a post area for details, or use the buttons below";
    } else {
        app.status_line = "Feed load failed";
        app.input_line = app.feed_result.error_message;
    }
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
    draw_centered_text(app, width / 2, 26, "sKeets", COLOR_WHITE, kColorHeader);
    draw_centered_text(app, width / 2, 26 + app.text_fb.font_h + 10, "Auth bootstrap shell", COLOR_WHITE, kColorHeader);

    draw_card(app, auth_rect, "Authentication", auth_lines(app));
    draw_card(app, device_rect, "Device", device_lines(app));
    draw_card(app, power_rect, "Power And Runtime", power_lines(app));
    draw_card(app, network_rect, "Connectivity", network_lines(app));

    rewrite_framebuffer_fill_rect(app.framebuffer, status_rect, kColorStatus);
    draw_border(app, status_rect, kColorCardBorder);
    font_draw_string(&app.text_fb,
                     status_rect.x + kCardPadding,
                     status_rect.y + 16,
                     app.status_line.c_str(),
                     COLOR_BLACK,
                     kColorStatus);
    font_draw_wrapped(&app.text_fb,
                      status_rect.x + kCardPadding,
                      status_rect.y + 16 + app.text_fb.font_h + 12,
                      status_rect.width - (kCardPadding * 2),
                      app.input_line.c_str(),
                      COLOR_BLACK,
                      kColorStatus,
                      4);

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

void render_feed_screen(rewrite_app_t& app, bool full_refresh) {
    const int width = app.framebuffer.info.screen_width;
    const int height = app.framebuffer.info.screen_height;
    const int header_height = std::max(72, height / 14);
    const int button_height = std::max(90, height / 12);
    const int content_top = header_height;
    const int content_bottom = height - button_height - kOuterMargin;
    const int content_width = width - (kOuterMargin * 2);
    const int line_spacing = 4;

    // Layout bottom buttons: [< Back] [Refresh] [Next >]
    const int btn_count = 3;
    const int btn_gap = kCardGap;
    const int btn_width = (width - (kOuterMargin * 2) - (btn_gap * (btn_count - 1))) / btn_count;
    const int btn_y = height - button_height - kOuterMargin;
    app.feed_back_button = {
        {kOuterMargin, btn_y, btn_width, button_height},
        "< Back"
    };
    app.feed_refresh_button = {
        {kOuterMargin + btn_width + btn_gap, btn_y, btn_width, button_height},
        "Refresh"
    };
    app.feed_next_button = {
        {kOuterMargin + (btn_width + btn_gap) * 2, btn_y, btn_width, button_height},
        "Next >"
    };

    rewrite_framebuffer_clear(app.framebuffer, COLOR_WHITE);

    // Header
    const rewrite_rect_t header_rect{0, 0, width, header_height};
    rewrite_framebuffer_fill_rect(app.framebuffer, header_rect, kColorHeader);
    draw_centered_text(app, width / 2, (header_height - app.text_fb.font_h) / 2,
                       "sKeets - Home Timeline", COLOR_WHITE, kColorHeader);

    // Posts
    const auto& items = app.feed_result.feed.items;
    const int total = static_cast<int>(items.size());
    int cursor_y = content_top + kOuterMargin;
    int rendered = 0;

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
            // Pre-measure to see if this post will fit
            int needed = 0;
            if (!post.reposted_by.empty()) {
                needed += app.text_fb.font_h + 2;
            }
            needed += app.text_fb.font_h + 4; // author line
            needed += font_measure_wrapped(content_width - 8, post.text.c_str(), line_spacing);
            needed += app.text_fb.font_h + kPostSeparator + 8; // stats + separator

            // Skip this post if it would overflow (except the first one — always show at least 1)
            if (rendered > 0 && cursor_y + needed > content_bottom) break;

            // Repost attribution
            if (!post.reposted_by.empty()) {
                std::string repost_line = sanitize_bitmap_text(">> Reposted by @" + post.reposted_by);
                font_draw_string(&app.text_fb, kOuterMargin, cursor_y,
                                 repost_line.c_str(), kColorRepost, COLOR_WHITE);
                cursor_y += app.text_fb.font_h + 2;
            }

            // Author line
            std::string author = post.author.display_name.empty()
                ? "@" + post.author.handle
                : post.author.display_name + "  @" + post.author.handle;
            author = sanitize_bitmap_text(author);

            char time_buf[64];
            str_format_time(time_buf, sizeof(time_buf), static_cast<long>(post.indexed_at));

            font_draw_string(&app.text_fb, kOuterMargin, cursor_y,
                             author.c_str(), kColorAuthor, COLOR_WHITE);
            // Draw timestamp right-aligned
            {
                int time_w = font_measure_string(time_buf);
                int time_x = width - kOuterMargin - time_w;
                if (time_x > kOuterMargin + font_measure_string(author.c_str()) + 8) {
                    font_draw_string(&app.text_fb, time_x, cursor_y,
                                     time_buf, kColorMeta, COLOR_WHITE);
                }
            }
            cursor_y += app.text_fb.font_h + 4;

            // Post text
            if (!post.text.empty()) {
                const std::string post_text = sanitize_bitmap_text(post.text);
                cursor_y = font_draw_wrapped(&app.text_fb,
                                             kOuterMargin + 4, cursor_y,
                                             content_width - 8,
                                             post_text.c_str(),
                                             COLOR_BLACK, COLOR_WHITE,
                                             line_spacing);
                cursor_y += 4;
            }

            // Embed indicator (text-only for now)
            if (post.embed_type == Bsky::EmbedType::Image) {
                font_draw_string(&app.text_fb, kOuterMargin + 4, cursor_y,
                                 "[Image]", kColorMeta, COLOR_WHITE);
                cursor_y += app.text_fb.font_h + 2;
            } else if (post.embed_type == Bsky::EmbedType::External) {
                std::string link = sanitize_bitmap_text("[Link: " + post.ext_title + "]");
                font_draw_string(&app.text_fb, kOuterMargin + 4, cursor_y,
                                 link.c_str(), kColorMeta, COLOR_WHITE);
                cursor_y += app.text_fb.font_h + 2;
            } else if (post.embed_type == Bsky::EmbedType::Quote && post.quoted_post) {
                std::string quote = sanitize_bitmap_text("[Quote: @" + post.quoted_post->author.handle + "]");
                font_draw_string(&app.text_fb, kOuterMargin + 4, cursor_y,
                                 quote.c_str(), kColorMeta, COLOR_WHITE);
                cursor_y += app.text_fb.font_h + 2;
            }

            // Stats line
            std::string stats;
            if (post.viewer_like.empty()) {
                stats += "  L:" + std::to_string(post.like_count);
            } else {
                stats += " *L:" + std::to_string(post.like_count);
            }
            stats += "  R:" + std::to_string(post.reply_count);
            stats += "  S:" + std::to_string(post.repost_count);
            font_draw_string(&app.text_fb, kOuterMargin + 4, cursor_y,
                             stats.c_str(), kColorMeta, COLOR_WHITE);
            cursor_y += app.text_fb.font_h;

            // Separator line
            cursor_y += kPostSeparator / 2;
            rewrite_framebuffer_fill_rect(app.framebuffer,
                rewrite_rect_t{kOuterMargin, cursor_y, content_width, 1},
                kColorPostBorder);
            cursor_y += kPostSeparator / 2;

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
        draw_centered_text(app, width / 2, info_y, page_info, kColorMeta, COLOR_WHITE);
    }

    // Buttons
    draw_button(app, app.feed_back_button, kColorButtonSecondary);
    draw_button(app, app.feed_refresh_button, kColorButtonSecondary);
    draw_button(app, app.feed_next_button, kColorButtonPrimary);

    std::string error_message;
    const rewrite_rect_t screen_rect = full_screen_rect(app);
    if (!rewrite_framebuffer_refresh(app.framebuffer,
                                     ui_refresh_mode(app, full_refresh),
                                     screen_rect,
                                     true,
                                     &error_message)) {
        std::fprintf(stderr, "rewrite app: feed refresh failed: %s\n", error_message.c_str());
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

        if (crypto && ssl) {
            std::fprintf(stderr, "rewrite openssl: preloaded OpenSSL 3.x from %s\n",
                         lib_dir.c_str());

            // Verify our preloaded libraries have the expected symbols
            void* sym = dlsym(ssl, "OPENSSL_init_ssl");
            std::fprintf(stderr, "rewrite openssl: dlsym(ssl_handle, OPENSSL_init_ssl) = %p\n", sym);
            sym = dlsym(RTLD_DEFAULT, "OPENSSL_init_ssl");
            std::fprintf(stderr, "rewrite openssl: dlsym(RTLD_DEFAULT, OPENSSL_init_ssl) = %p\n", sym);

            // Check what the dynamic linker finds for each name Qt might try
            const char* probe_names[] = {"libssl.so.3", "libssl.so", "libcrypto.so.3", "libcrypto.so"};
            for (const char* name : probe_names) {
                void* h = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
                std::fprintf(stderr, "rewrite openssl: dlopen(\"%s\", NOLOAD) = %p\n", name, h);
                if (!h) {
                    // Try actual load to see what we'd get
                    h = dlopen(name, RTLD_NOW);
                    std::fprintf(stderr, "rewrite openssl: dlopen(\"%s\", NOW) = %p err=%s\n",
                                 name, h, h ? "ok" : dlerror());
                    if (h) {
                        void* ver = dlsym(h, "OpenSSL_version_num");
                        if (ver) {
                            auto fn = reinterpret_cast<unsigned long (*)()>(ver);
                            std::fprintf(stderr, "rewrite openssl: %s version_num=0x%08lx\n",
                                         name, fn());
                        } else {
                            // OpenSSL 1.0.x uses SSLeay instead
                            void* legacy = dlsym(h, "SSLeay");
                            std::fprintf(stderr, "rewrite openssl: %s has SSLeay=%p (1.0.x)\n",
                                         name, legacy);
                        }
                    }
                }
            }
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
                std::fprintf(stderr, "rewrite ssl: loaded %d CA certs from %s\n",
                             certs.size(), ca_path.toUtf8().constData());
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
    font_init(&rewrite_app.text_fb);
    font_set_ot_enabled(false);

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

    // If auth succeeded, auto-load the feed and switch to feed view
    if (rewrite_app.bootstrap.authenticated) {
        render_screen(rewrite_app, true);  // Show dashboard briefly with auth status
        load_feed(rewrite_app);
        if (rewrite_app.feed_result.state == rewrite_feed_state_t::loaded) {
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
        rewrite_input_event_t event;
        error_message.clear();
        const bool have_event = rewrite_input_poll(rewrite_app.input, event, 500, &error_message);
        if (!have_event) {
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
            if (contains_point(rewrite_app.feed_back_button.rect, event.x, event.y)) {
                rewrite_app.view_mode = rewrite_view_mode_t::dashboard;
                rewrite_app.status_line = rewrite_app.bootstrap.headline;
                rewrite_app.input_line = rewrite_app.bootstrap.detail;
                render_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.feed_refresh_button.rect, event.x, event.y)) {
                render_feed_screen(rewrite_app, false);  // Show current state
                load_feed(rewrite_app);
                render_feed_screen(rewrite_app, true);
                continue;
            }

            if (contains_point(rewrite_app.feed_next_button.rect, event.x, event.y)) {
                const int total = static_cast<int>(rewrite_app.feed_result.feed.items.size());
                int next_start = rewrite_app.feed_page_start + rewrite_app.feed_page_count;
                if (next_start < total) {
                    rewrite_app.feed_page_start = next_start;
                    render_feed_screen(rewrite_app, true);
                } else if (!rewrite_app.feed_result.feed.cursor.empty()) {
                    // Load more posts from the server
                    auto more = rewrite_fetch_feed(rewrite_app.bootstrap.session, 30,
                                                   rewrite_app.feed_result.feed.cursor);
                    if (more.state == rewrite_feed_state_t::loaded) {
                        rewrite_app.feed_page_start = total;  // Jump to newly loaded posts
                        for (auto& p : more.feed.items) {
                            rewrite_app.feed_result.feed.items.push_back(std::move(p));
                        }
                        rewrite_app.feed_result.feed.cursor = more.feed.cursor;
                        rewrite_app.feed_result.post_count = static_cast<int>(
                            rewrite_app.feed_result.feed.items.size());
                    }
                    render_feed_screen(rewrite_app, true);
                }
                continue;
            }

            // Tap anywhere else in feed — no action for now
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
    rewrite_framebuffer_close(rewrite_app.framebuffer);
    return 0;
}