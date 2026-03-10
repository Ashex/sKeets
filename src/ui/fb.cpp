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

    size_t buf_size = 0;
    unsigned char *ptr = fbink_get_fb_pointer(fb->fd, &buf_size);
    if (!ptr) {
        fprintf(stderr, "fb_open: fbink_get_fb_pointer failed\n");
        fbink_close(fb->fd);
        fb->fd = -1;
        return -1;
    }
    fb->mem = reinterpret_cast<uint16_t*>(ptr);

    return 0;
}

void fb_close(fb_t *fb) {
    if (!fb) return;
    fb->mem = NULL;  /* FBInk owns the framebuffer mapping */
    if (fb->fd >= 0) { fbink_close(fb->fd); fb->fd = -1; }
}

/* ── Drawing primitives ───────────────────────────────────────────── */

void fb_clear(fb_t *fb, uint16_t color) {
    if (!fb || !fb->mem) return;
    int n = fb->width * fb->height;
    for (int i = 0; i < n; i++)
        fb->mem[i] = color;
}

void fb_fill_rect(fb_t *fb, int x, int y, int w, int h, uint16_t color) {
    if (!fb || !fb->mem || w <= 0 || h <= 0) return;
    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb->width)  w = fb->width  - x;
    if (y + h > fb->height) h = fb->height - y;
    if (w <= 0 || h <= 0) return;

    for (int row = y; row < y + h; row++) {
        uint16_t *line = fb->mem + row * fb->width + x;
        for (int col = 0; col < w; col++)
            line[col] = color;
    }
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

void fb_blit(fb_t *fb, int x, int y, int w, int h, const uint16_t *pixels) {
    if (!fb || !fb->mem || !pixels || w <= 0 || h <= 0) return;
    /* Clip */
    int src_x = 0, src_y = 0;
    if (x < 0) { src_x = -x; w += x; x = 0; }
    if (y < 0) { src_y = -y; h += y; y = 0; }
    if (x + w > fb->width)  w = fb->width  - x;
    if (y + h > fb->height) h = fb->height - y;
    if (w <= 0 || h <= 0) return;

    int src_stride = w + src_x * 2; /* original width before clipping */
    (void)src_stride;

    for (int row = 0; row < h; row++) {
        const uint16_t *src = pixels + (src_y + row) * (w + src_x) + src_x;
        uint16_t       *dst = fb->mem + (y + row) * fb->width + x;
        memcpy(dst, src, (size_t)w * sizeof(uint16_t));
    }
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
