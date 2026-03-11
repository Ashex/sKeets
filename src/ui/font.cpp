#include "font.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fbink.h>

#define FONT_SIZE_PX 14

static int s_fbfd     = -1;
static int s_screen_w = 1448;
static int s_screen_h = 1072;
static bool s_ot_enabled = false;

/* Map an RGB565 color to 8-bit grayscale luminance. */
static uint8_t rgb565_to_gray(uint16_t c) {
    uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
    uint8_t b = (uint8_t)((c & 0x1F) << 3);
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

static FG_COLOR_INDEX_T gray_to_fg(uint8_t gray) {
    return (FG_COLOR_INDEX_T)(gray / 17U);
}

static BG_COLOR_INDEX_T gray_to_bg(uint8_t gray) {
    return (BG_COLOR_INDEX_T)((255U - gray) / 17U);
}

static int font_draw_string_fallback(fb_t *fb, int x, int y, const char *s,
                                     uint16_t fg, uint16_t bg) {
    if (!fb || fb->fd < 0 || !s || !*s) return x;

    FBInkConfig cfg = {};
    cfg.no_refresh  = true;
    cfg.row         = (short int)(y / FONT_CHAR_H);
    cfg.col         = (short int)(x / FONT_CHAR_W);
    cfg.hoffset     = (short int)(x % FONT_CHAR_W);
    cfg.voffset     = (short int)(y % FONT_CHAR_H);
    cfg.fontname    = VGA;
    cfg.fg_color    = gray_to_fg(rgb565_to_gray(fg));
    cfg.bg_color    = gray_to_bg(rgb565_to_gray(bg));
    cfg.is_bgless   = false;

    int ret = fbink_print(fb->fd, s, &cfg);
    if (ret < 0) {
        fprintf(stderr, "font_draw_string: fbink_print fallback failed: %d\n", ret);
    }

    return x + (int)strlen(s) * FONT_CHAR_W;
}

static int font_draw_wrapped_fallback(fb_t *fb, int x, int y, int max_w,
                                      const char *s, uint16_t fg, uint16_t bg,
                                      int line_spacing) {
    if (!s || !*s || max_w <= 0) return y;

    const int chars_per_line = max_w / FONT_CHAR_W > 0 ? max_w / FONT_CHAR_W : 1;
    char line[512];
    int line_len = 0;
    int cursor_y = y;
    const char *word = s;

    while (*word) {
        while (*word == ' ') word++;
        if (!*word) break;

        const char *word_end = word;
        while (*word_end && *word_end != ' ') word_end++;
        int word_len = (int)(word_end - word);

        if (line_len > 0 && line_len + 1 + word_len > chars_per_line) {
            line[line_len] = '\0';
            font_draw_string_fallback(fb, x, cursor_y, line, fg, bg);
            cursor_y += FONT_CHAR_H + line_spacing;
            line_len = 0;
        }

        if (word_len > chars_per_line) {
            int offset = 0;
            while (offset < word_len) {
                int chunk = chars_per_line;
                if (chunk > word_len - offset) chunk = word_len - offset;
                memcpy(line, word + offset, (size_t)chunk);
                line[chunk] = '\0';
                font_draw_string_fallback(fb, x, cursor_y, line, fg, bg);
                cursor_y += FONT_CHAR_H + line_spacing;
                offset += chunk;
            }
            word = word_end;
            continue;
        }

        if (line_len > 0) line[line_len++] = ' ';
        memcpy(line + line_len, word, (size_t)word_len);
        line_len += word_len;
        word = word_end;
    }

    if (line_len > 0) {
        line[line_len] = '\0';
        font_draw_string_fallback(fb, x, cursor_y, line, fg, bg);
        cursor_y += FONT_CHAR_H + line_spacing;
    }

    return cursor_y;
}

/* ── Init ─────────────────────────────────────────────────────────── */

void font_init(fb_t *fb) {
    if (!fb) return;
    s_fbfd     = fb->fd;
    s_screen_w = fb->width;
    s_screen_h = fb->height;
}

void font_set_ot_enabled(bool enabled) {
    s_ot_enabled = enabled;
}

/* ── Public API ───────────────────────────────────────────────────── */

int font_draw_char(fb_t *fb, int x, int y, char c,
                   uint16_t fg, uint16_t bg) {
    char s[2] = { c, '\0' };
    int end_x = font_draw_string(fb, x, y, s, fg, bg);
    return end_x - x;
}

int font_draw_string(fb_t *fb, int x, int y, const char *s,
                     uint16_t fg, uint16_t bg) {
    if (!s || !*s) return x;
    if (!fb || fb->fd < 0) return x + (int)strlen(s) * FONT_CHAR_W;
    if (!s_ot_enabled) return font_draw_string_fallback(fb, x, y, s, fg, bg);

    FBInkOTConfig ot_cfg = {};
    ot_cfg.size_px  = FONT_SIZE_PX;
    ot_cfg.margins.left   = (short int)x;
    ot_cfg.margins.top    = (short int)y;
    ot_cfg.margins.right  = 0;
    short int bot = (short int)(fb->height - y - FONT_CHAR_H * 2);
    ot_cfg.margins.bottom = bot > 0 ? bot : 0;

    FBInkConfig cfg = {};
    cfg.no_refresh = true;
    cfg.is_bgless  = true;
    cfg.fg_color   = rgb565_to_gray(fg);

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
    if (!s || !*s) return 0;
    if (s_fbfd < 0 || !s_ot_enabled) return (int)strlen(s) * FONT_CHAR_W;

    FBInkOTConfig ot_cfg = {};
    ot_cfg.size_px      = FONT_SIZE_PX;
    ot_cfg.margins      = {};
    ot_cfg.compute_only = true;

    FBInkOTFit fit = {};
    int ret = fbink_print_ot(s_fbfd, s, &ot_cfg, NULL, &fit);
    if (ret == -ENODATA || ret == -ENOSYS) {
        s_ot_enabled = false;
        return (int)strlen(s) * FONT_CHAR_W;
    }

    return fit.bbox.width > 0 ? (int)fit.bbox.width : (int)strlen(s) * FONT_CHAR_W;
}

int font_draw_wrapped(fb_t *fb, int x, int y, int max_w,
                      const char *s, uint16_t fg, uint16_t bg,
                      int line_spacing) {
    if (!s || !*s || max_w <= 0) return y;
    if (!fb || fb->fd < 0) return y + FONT_CHAR_H;
    if (!s_ot_enabled) return font_draw_wrapped_fallback(fb, x, y, max_w, s, fg, bg, line_spacing);

    FBInkOTConfig ot_cfg = {};
    ot_cfg.size_px = FONT_SIZE_PX;
    ot_cfg.margins.left   = (short int)x;
    ot_cfg.margins.top    = (short int)y;
    short int right = (short int)(fb->width - x - max_w);
    ot_cfg.margins.right  = right > 0 ? right : 0;
    ot_cfg.margins.bottom = 0;

    FBInkConfig cfg = {};
    cfg.no_refresh = true;
    cfg.is_bgless  = true;
    cfg.fg_color   = rgb565_to_gray(fg);

    FBInkOTFit fit = {};
    int ret = fbink_print_ot(fb->fd, s, &ot_cfg, &cfg, &fit);
    if (ret == -ENODATA || ret == -ENOSYS) {
        fprintf(stderr, "font_draw_wrapped: OpenType unavailable, using fallback\n");
        s_ot_enabled = false;
        return font_draw_wrapped_fallback(fb, x, y, max_w, s, fg, bg, line_spacing);
    }

    /* fbink_print_ot returns the new top margin (= y after text) on success. */
    if (ret > y) return ret;
    if (fit.bbox.height > 0) return y + (int)fit.bbox.height;
    return y + FONT_CHAR_H;
}

int font_measure_wrapped(int max_w, const char *s, int line_spacing) {
    if (!s || !*s || max_w <= 0) return FONT_CHAR_H;
    if (s_fbfd < 0 || !s_ot_enabled) {
        /* Fallback: approximate with constant-width characters. */
        int chars_per_line = max_w / FONT_CHAR_W;
        if (chars_per_line <= 0) chars_per_line = 1;
        int len   = (int)strlen(s);
        int lines = (len + chars_per_line - 1) / chars_per_line;
        if (lines < 1) lines = 1;
        return lines * (FONT_CHAR_H + line_spacing);
    }

    FBInkOTConfig ot_cfg = {};
    ot_cfg.size_px      = FONT_SIZE_PX;
    ot_cfg.margins.left = 0;
    ot_cfg.margins.top  = 0;
    short int right = (short int)(s_screen_w - max_w);
    ot_cfg.margins.right  = right > 0 ? right : 0;
    ot_cfg.margins.bottom = 0;
    ot_cfg.compute_only   = true;

    FBInkOTFit fit = {};
    int ret = fbink_print_ot(s_fbfd, s, &ot_cfg, NULL, &fit);
    if (ret == -ENODATA || ret == -ENOSYS) {
        s_ot_enabled = false;
        int chars_per_line = max_w / FONT_CHAR_W;
        if (chars_per_line <= 0) chars_per_line = 1;
        int len   = (int)strlen(s);
        int lines = (len + chars_per_line - 1) / chars_per_line;
        if (lines < 1) lines = 1;
        return lines * (FONT_CHAR_H + line_spacing);
    }

    if (fit.bbox.height > 0) return (int)fit.bbox.height;
    return FONT_CHAR_H;
}
