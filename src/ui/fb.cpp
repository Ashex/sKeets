#include "fb.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

/* ── MXCFB structures (Kobo/Freescale e-ink driver) ─────────────── */

#define MXCFB_SEND_UPDATE   _IOW('F', 0x2E, struct mxcfb_update_data)

struct mxcfb_rect {
    uint32_t top;
    uint32_t left;
    uint32_t width;
    uint32_t height;
};

#define UPDATE_MODE_PARTIAL  0x0
#define UPDATE_MODE_FULL     0x1

struct mxcfb_update_data {
    struct mxcfb_rect update_region;
    uint32_t          waveform_mode;
    uint32_t          update_mode;
    uint32_t          update_marker;
    int               temp;
    uint32_t          flags;
    void             *alt_buffer_data;  /* unused */
};

/* ── Open / close ─────────────────────────────────────────────────── */

int fb_open(fb_t *fb) {
    if (!fb) return -1;
    memset(fb, 0, sizeof(*fb));

    fb->fd = open("/dev/fb0", O_RDWR);
    if (fb->fd < 0) return -1;

    struct fb_var_screeninfo vinfo;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        close(fb->fd);
        return -1;
    }
    fb->width  = (int)vinfo.xres;
    fb->height = (int)vinfo.yres;

    size_t fb_size = (size_t)(fb->width * fb->height * (FB_BPP / 8));
    fb->mem = static_cast<uint16_t*>(mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0));
    if (fb->mem == MAP_FAILED) {
        close(fb->fd);
        fb->mem = NULL;
        return -1;
    }
    return 0;
}

void fb_close(fb_t *fb) {
    if (!fb) return;
    if (fb->mem) {
        size_t fb_size = (size_t)(fb->width * fb->height * (FB_BPP / 8));
        munmap(fb->mem, fb_size);
        fb->mem = NULL;
    }
    if (fb->fd >= 0) { close(fb->fd); fb->fd = -1; }
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

/* ── E-ink refresh ───────────────────────────────────────────────── */

static const uint32_t FIRST_UPDATE_MARKER = 1;

static void fb_refresh_region(fb_t *fb, int x, int y, int w, int h,
                               int waveform, uint32_t update_mode) {
    if (!fb || fb->fd < 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    struct mxcfb_update_data upd = {0};
    upd.update_region.left   = (uint32_t)x;
    upd.update_region.top    = (uint32_t)y;
    upd.update_region.width  = (uint32_t)w;
    upd.update_region.height = (uint32_t)h;
    upd.waveform_mode        = (uint32_t)waveform;
    upd.update_mode          = update_mode;
    upd.update_marker        = FIRST_UPDATE_MARKER;
    upd.temp                 = TEMP_USE_AMBIENT;
    upd.flags                = 0;

    ioctl(fb->fd, MXCFB_SEND_UPDATE, &upd);
}

void fb_refresh_full(fb_t *fb) {
    fb_refresh_region(fb, 0, 0, fb->width, fb->height,
                      WAVEFORM_MODE_GC16, UPDATE_MODE_FULL);
}

void fb_refresh_partial(fb_t *fb, int x, int y, int w, int h) {
    fb_refresh_region(fb, x, y, w, h, WAVEFORM_MODE_DU, UPDATE_MODE_PARTIAL);
}
