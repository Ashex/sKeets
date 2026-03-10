#include "fb.h"

#include <string.h>
#include <stdio.h>
#include <fbink.h>

/* ── Open / close ─────────────────────────────────────────────────── */

int fb_open(fb_t *fb) {
    if (!fb) return -1;
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;

    fb->fd = fbink_open();
    if (fb->fd < 0) {
        fprintf(stderr, "fb_open: fbink_open failed\n");
        return -1;
    }

    FBInkConfig cfg = {};
    cfg.is_quiet = true;
    if (fbink_init(fb->fd, &cfg) < 0) {
        fprintf(stderr, "fb_open: fbink_init failed\n");
        fbink_close(fb->fd);
        fb->fd = -1;
        return -1;
    }

    FBInkState state = {};
    fbink_get_state(&cfg, &state);
    fb->width  = (int)state.screen_width;
    fb->height = (int)state.screen_height;

    return 0;
}

void fb_close(fb_t *fb) {
    if (!fb) return;
    fbink_free_ot_fonts();
    if (fb->fd >= 0) { fbink_close(fb->fd); fb->fd = -1; }
}

int fb_load_fonts(fb_t *fb, const char *font_dir) {
    if (!fb || fb->fd < 0 || !font_dir) return -1;

    char path[512];
    snprintf(path, sizeof(path), "%s/NotoSans-Regular.ttf", font_dir);
    if (fbink_add_ot_font(path, FNT_REGULAR) < 0) {
        fprintf(stderr, "fb_load_fonts: failed to load regular font: %s\n", path);
        return -1;
    }

    snprintf(path, sizeof(path), "%s/NotoSans-Bold.ttf", font_dir);
    if (fbink_add_ot_font(path, FNT_BOLD) < 0)
        fprintf(stderr, "fb_load_fonts: warning: bold font not found: %s\n", path);

    snprintf(path, sizeof(path), "%s/NotoSans-Italic.ttf", font_dir);
    if (fbink_add_ot_font(path, FNT_ITALIC) < 0)
        fprintf(stderr, "fb_load_fonts: warning: italic font not found: %s\n", path);

    return 0;
}

/* ── Helpers ───────────────────────────────────────────────────────── */

/* Map an RGB565 color to 8-bit grayscale luminance. */
static uint8_t rgb565_to_gray(uint16_t c) {
    uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
    uint8_t b = (uint8_t)((c & 0x1F) << 3);
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

/* ── Drawing primitives ───────────────────────────────────────────── */

void fb_clear(fb_t *fb, uint16_t color) {
    if (!fb || fb->fd < 0) return;
    FBInkConfig cfg = {};
    cfg.no_refresh = true;
    cfg.bg_color = rgb565_to_gray(color);
    fbink_cls(fb->fd, &cfg, NULL, false);
}

void fb_fill_rect(fb_t *fb, int x, int y, int w, int h, uint16_t color) {
    if (!fb || fb->fd < 0 || w <= 0 || h <= 0) return;
    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb->width)  w = fb->width  - x;
    if (y + h > fb->height) h = fb->height - y;
    if (w <= 0 || h <= 0) return;

    FBInkConfig cfg = {};
    cfg.no_refresh = true;

    FBInkRect rect = {};
    rect.left   = (unsigned short int)x;
    rect.top    = (unsigned short int)y;
    rect.width  = (unsigned short int)w;
    rect.height = (unsigned short int)h;

    fbink_fill_rect_gray(fb->fd, &cfg, &rect, false, rgb565_to_gray(color));
}

void fb_draw_rect(fb_t *fb, int x, int y, int w, int h, uint16_t color, int thickness) {
    if (thickness <= 0) thickness = 1;
    fb_fill_rect(fb, x,             y,             w,         thickness, color); /* top */
    fb_fill_rect(fb, x,             y + h - thickness, w,     thickness, color); /* bottom */
    fb_fill_rect(fb, x,             y,             thickness, h,         color); /* left */
    fb_fill_rect(fb, x + w - thickness, y,         thickness, h,         color); /* right */
}

void fb_hline(fb_t *fb, int x, int y, int len, uint16_t color) {
    fb_fill_rect(fb, x, y, len, 1, color);
}

void fb_vline(fb_t *fb, int x, int y, int len, uint16_t color) {
    fb_fill_rect(fb, x, y, 1, len, color);
}

void fb_blit_rgba(fb_t *fb, int x, int y, int w, int h, const uint8_t *rgba) {
    if (!fb || fb->fd < 0 || !rgba || w <= 0 || h <= 0) return;

    FBInkConfig cfg = {};
    cfg.no_refresh = true;
    cfg.ignore_alpha = false;

    fbink_print_raw_data(fb->fd, rgba, w, h,
                         (size_t)(w * h * 4),
                         (short int)x, (short int)y, &cfg);
}

/* ── E-ink refresh (via FBInk) ────────────────────────────────────── */

void fb_refresh_full(fb_t *fb) {
    if (!fb || fb->fd < 0) return;
    FBInkConfig cfg = {};
    cfg.is_flashing = true;
    /* Empty region (0×0 @ 0,0) triggers a full-screen refresh. */
    fbink_refresh(fb->fd, 0, 0, 0, 0, &cfg);
}

void fb_refresh_partial(fb_t *fb, int x, int y, int w, int h) {
    if (!fb || fb->fd < 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    FBInkConfig cfg = {};
    cfg.wfm_mode = WFM_DU;
    fbink_refresh(fb->fd, (uint32_t)y, (uint32_t)x, (uint32_t)w, (uint32_t)h, &cfg);
}

void fb_refresh_fast(fb_t *fb, int x, int y, int w, int h) {
    if (!fb || fb->fd < 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    FBInkConfig cfg = {};
    cfg.wfm_mode = WFM_A2;
    fbink_refresh(fb->fd, (uint32_t)y, (uint32_t)x, (uint32_t)w, (uint32_t)h, &cfg);
}
