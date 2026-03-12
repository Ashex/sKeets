#include "font.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <algorithm>
#include <string>
#include <vector>
#include <fbink.h>

#define FONT_SIZE_PX 28

static int s_fbfd     = -1;
static int s_screen_w = 1448;
static int s_font_w   = 8;
static int s_font_h   = 16;
static bool s_ot_enabled = false;

int font_measure_string(const char *s);
int font_measure_string_styled(const char *s, int size_px, font_style_t style);
static std::vector<std::string> wrap_text_lines(int max_w, const char *s, int size_px, font_style_t style);

static FONT_STYLE_T map_font_style(font_style_t style) {
    switch (style) {
    case FONT_STYLE_EMOJI:
        return FNT_BOLD_ITALIC;
    case FONT_STYLE_MEDIUM:
        return FNT_BOLD;
    case FONT_STYLE_LIGHT:
        return FNT_ITALIC;
    case FONT_STYLE_REGULAR:
    default:
        return FNT_REGULAR;
    }
}

static int effective_font_size(int size_px) {
    return size_px > 0 ? size_px : FONT_SIZE_PX;
}

static FG_COLOR_INDEX_T gray_to_fg(uint8_t gray) {
    return (FG_COLOR_INDEX_T)(gray / 17U);
}

static BG_COLOR_INDEX_T gray_to_bg(uint8_t gray) {
    return (BG_COLOR_INDEX_T)((255U - gray) / 17U);
}

static int font_draw_string_fallback(fb_t *fb, int x, int y, const char *s,
                                     uint8_t fg, uint8_t bg) {
    if (!fb || fb->fd < 0 || !s || !*s) return x;

    FBInkConfig cfg = {};
    cfg.no_refresh  = true;
    cfg.row         = (short int)(y / s_font_h);
    cfg.col         = (short int)(x / s_font_w);
    cfg.hoffset     = (short int)(x % s_font_w);
    cfg.voffset     = (short int)(y % s_font_h);
    cfg.fontname    = VGA;
    cfg.fg_color    = gray_to_fg(fg);
    cfg.bg_color    = gray_to_bg(bg);
    cfg.is_bgless   = false;

    int ret = fbink_print(fb->fd, s, &cfg);
    if (ret < 0) {
        fprintf(stderr, "font_draw_string: fbink_print fallback failed: %d\n", ret);
    }

    return x + (int)strlen(s) * s_font_w;
}

static int font_draw_wrapped_fallback(fb_t *fb, int x, int y, int max_w,
                                      const char *s, uint8_t fg, uint8_t bg,
                                      int line_spacing,
                                      int size_px,
                                      font_style_t style) {
    if (!s || !*s || max_w <= 0) return y;

    const auto lines = wrap_text_lines(max_w, s, size_px, style);
    int cursor_y = y;
    for (size_t index = 0; index < lines.size(); ++index) {
        if (!lines[index].empty()) {
            font_draw_string_fallback(fb, x, cursor_y, lines[index].c_str(), fg, bg);
        }
        cursor_y += s_font_h;
        if (index + 1 < lines.size()) {
            cursor_y += std::max(0, line_spacing);
        }
    }

    return cursor_y;
}

static std::vector<std::string> wrap_text_lines(int max_w, const char *s, int size_px, font_style_t style) {
    std::vector<std::string> lines;
    if (!s || !*s || max_w <= 0) {
        lines.push_back("");
        return lines;
    }

    auto push_wrapped_word = [&](const std::string& word) {
        if (word.empty()) return;
        size_t start = 0;
        while (start < word.size()) {
            size_t best = start + 1;
            for (size_t end = start + 1; end <= word.size(); ++end) {
                const std::string candidate = word.substr(start, end - start);
                if (font_measure_string_styled(candidate.c_str(), size_px, style) <= max_w) {
                    best = end;
                    continue;
                }
                break;
            }
            if (best <= start) best = start + 1;
            lines.push_back(word.substr(start, best - start));
            start = best;
        }
    };

    std::string current;
    const char *cursor = s;
    while (*cursor) {
        if (*cursor == '\n') {
            lines.push_back(current);
            current.clear();
            ++cursor;
            continue;
        }

        while (*cursor == ' ') ++cursor;
        if (!*cursor) break;
        if (*cursor == '\n') continue;

        const char *word_end = cursor;
        while (*word_end && *word_end != ' ' && *word_end != '\n') ++word_end;
        const std::string word(cursor, (size_t)(word_end - cursor));
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (font_measure_string_styled(candidate.c_str(), size_px, style) <= max_w) {
            current = candidate;
        } else {
            if (!current.empty()) {
                lines.push_back(current);
                current.clear();
            }
            if (font_measure_string_styled(word.c_str(), size_px, style) <= max_w) {
                current = word;
            } else {
                push_wrapped_word(word);
            }
        }
        cursor = word_end;
    }

    if (!current.empty()) {
        lines.push_back(current);
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

static int wrapped_text_height_for_lines(size_t line_count, int line_spacing) {
    if (line_count == 0) return s_font_h;
    return (int)line_count * s_font_h + (int)(line_count - 1) * std::max(0, line_spacing);
}

static int font_line_height_internal(int size_px, font_style_t style) {
    if (s_fbfd < 0 || !s_ot_enabled) {
        return s_font_h;
    }

    FBInkOTConfig ot_cfg = {};
    ot_cfg.size_px = static_cast<unsigned short int>(effective_font_size(size_px));
    ot_cfg.style = map_font_style(style);
    ot_cfg.compute_only = true;

    FBInkOTFit fit = {};
    int ret = fbink_print_ot(s_fbfd, "Ag", &ot_cfg, NULL, &fit);
    if (ret == -ENODATA || ret == -ENOSYS) {
        s_ot_enabled = false;
        return s_font_h;
    }

    return fit.bbox.height > 0 ? (int)fit.bbox.height : std::max(s_font_h, effective_font_size(size_px));
}

/* ── Init ─────────────────────────────────────────────────────────── */

void font_init(fb_t *fb) {
    if (!fb) return;
    s_fbfd     = fb->fd;
    s_screen_w = fb->width;
    s_font_w   = fb->font_w;
    s_font_h   = fb->font_h;
}

void font_set_ot_enabled(bool enabled) {
    s_ot_enabled = enabled;
}

int font_cell_w(void) { return s_font_w; }
int font_cell_h(void) { return s_font_h; }
int font_line_height(int size_px, font_style_t style) { return font_line_height_internal(size_px, style); }

/* ── Public API ───────────────────────────────────────────────────── */

int font_draw_char(fb_t *fb, int x, int y, char c,
                   uint8_t fg, uint8_t bg) {
    char s[2] = { c, '\0' };
    int end_x = font_draw_string(fb, x, y, s, fg, bg);
    return end_x - x;
}

int font_draw_string(fb_t *fb, int x, int y, const char *s,
                     uint8_t fg, uint8_t bg) {
    return font_draw_string_styled(fb, x, y, s, fg, bg, FONT_SIZE_PX, FONT_STYLE_REGULAR);
}

int font_draw_string_styled(fb_t *fb, int x, int y, const char *s,
                            uint8_t fg, uint8_t bg,
                            int size_px,
                            font_style_t style) {
    if (!s || !*s) return x;
    if (!fb || fb->fd < 0) return x + (int)strlen(s) * s_font_w;
    if (!s_ot_enabled) return font_draw_string_fallback(fb, x, y, s, fg, bg);

    FBInkOTConfig ot_cfg = {};
    ot_cfg.size_px  = static_cast<unsigned short int>(effective_font_size(size_px));
    ot_cfg.style    = map_font_style(style);
    ot_cfg.margins.left   = (short int)x;
    ot_cfg.margins.top    = (short int)y;
    ot_cfg.margins.right  = 0;
    const int line_height = font_line_height_internal(size_px, style);
    short int bot = (short int)(fb->height - y - line_height * 2);
    ot_cfg.margins.bottom = bot > 0 ? bot : 0;

    FBInkConfig cfg = {};
    cfg.no_refresh = true;
    cfg.is_bgless  = false;
    cfg.fg_color   = gray_to_fg(fg);
    cfg.bg_color   = gray_to_bg(bg);

    FBInkOTFit fit = {};
    int ret = fbink_print_ot(fb->fd, s, &ot_cfg, &cfg, &fit);
    if (ret == -ENODATA || ret == -ENOSYS) {
        fprintf(stderr, "font_draw_string: OpenType unavailable, using fallback\n");
        s_ot_enabled = false;
        return font_draw_string_fallback(fb, x, y, s, fg, bg);
    }

    return x + (int)fit.bbox.width;
}

int font_measure_string(const char *s) {
    return font_measure_string_styled(s, FONT_SIZE_PX, FONT_STYLE_REGULAR);
}

int font_measure_string_styled(const char *s, int size_px, font_style_t style) {
    if (!s || !*s) return 0;
    if (s_fbfd < 0 || !s_ot_enabled) return (int)strlen(s) * s_font_w;

    FBInkOTConfig ot_cfg = {};
    ot_cfg.size_px      = static_cast<unsigned short int>(effective_font_size(size_px));
    ot_cfg.style        = map_font_style(style);
    ot_cfg.margins      = {};
    ot_cfg.compute_only = true;

    FBInkOTFit fit = {};
    int ret = fbink_print_ot(s_fbfd, s, &ot_cfg, NULL, &fit);
    if (ret == -ENODATA || ret == -ENOSYS) {
        s_ot_enabled = false;
        return (int)strlen(s) * s_font_w;
    }

    return fit.bbox.width > 0 ? (int)fit.bbox.width : (int)strlen(s) * s_font_w;
}

int font_draw_wrapped(fb_t *fb, int x, int y, int max_w,
                      const char *s, uint8_t fg, uint8_t bg,
                      int line_spacing) {
    return font_draw_wrapped_styled(fb, x, y, max_w, s, fg, bg, line_spacing, FONT_SIZE_PX, FONT_STYLE_REGULAR);
}

int font_draw_wrapped_styled(fb_t *fb, int x, int y, int max_w,
                             const char *s, uint8_t fg, uint8_t bg,
                             int line_spacing,
                             int size_px,
                             font_style_t style) {
    if (!s || !*s || max_w <= 0) return y;
    if (!fb || fb->fd < 0) return y + s_font_h;
    if (!s_ot_enabled) return font_draw_wrapped_fallback(fb, x, y, max_w, s, fg, bg, line_spacing, size_px, style);

    const auto lines = wrap_text_lines(max_w, s, size_px, style);
    int cursor_y = y;
    const int line_height = font_line_height_internal(size_px, style);
    for (size_t index = 0; index < lines.size(); ++index) {
        if (!lines[index].empty()) {
            font_draw_string_styled(fb, x, cursor_y, lines[index].c_str(), fg, bg, size_px, style);
        }
        cursor_y += line_height;
        if (index + 1 < lines.size()) {
            cursor_y += std::max(0, line_spacing);
        }
    }
    return cursor_y;
}

int font_measure_wrapped(int max_w, const char *s, int line_spacing) {
    return font_measure_wrapped_styled(max_w, s, line_spacing, FONT_SIZE_PX, FONT_STYLE_REGULAR);
}

int font_measure_wrapped_styled(int max_w, const char *s, int line_spacing,
                                int size_px,
                                font_style_t style) {
    if (!s || !*s || max_w <= 0) return s_font_h;
    const auto lines = wrap_text_lines(max_w, s, size_px, style);
    const int line_height = font_line_height_internal(size_px, style);
    if (s_fbfd < 0 || !s_ot_enabled) {
        return wrapped_text_height_for_lines(lines.size(), line_spacing);
    }

    if (lines.empty()) return line_height;
    return (int)lines.size() * line_height + (int)(lines.size() - 1) * std::max(0, line_spacing);
}
