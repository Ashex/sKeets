#ifndef SKEETS_FB_H
#define SKEETS_FB_H

#include <stdint.h>
#include <stdbool.h>

/* ── Display constants ───────────────────────────────────────────── */
#define FB_WIDTH    1448
#define FB_HEIGHT   1072
#define FB_BPP      16     /* RGB565 */
#define FB_STRIDE   (FB_WIDTH * (FB_BPP / 8))

/* ── MXCFB e-ink waveform modes ─────────────────────────────────── */
#define WAVEFORM_MODE_INIT  0x0  /* Full init flash */
#define WAVEFORM_MODE_DU    0x1  /* Direct update – fast, monochrome */
#define WAVEFORM_MODE_GC16  0x2  /* High quality 16-level grayscale */
#define WAVEFORM_MODE_GL16  0x3  /* GL16 mode */
#define WAVEFORM_MODE_A2    0x6  /* Very fast, binary only */
#define WAVEFORM_MODE_AUTO  0xFF

#define TEMP_USE_AMBIENT 0x1000

/* ── Colors (RGB565) ─────────────────────────────────────────────── */
#define COLOR_BLACK  ((uint16_t)0x0000)
#define COLOR_WHITE  ((uint16_t)0xFFFF)
#define COLOR_GRAY   ((uint16_t)0x8410)  /* mid-gray ~128,128,128 */
#define COLOR_LGRAY  ((uint16_t)0xC618)  /* light gray */
#define COLOR_DGRAY  ((uint16_t)0x4208)  /* dark gray */

/* ── Framebuffer context ─────────────────────────────────────────── */
typedef struct {
    int       fd;
    uint16_t *mem;        /* mmap'd framebuffer */
    int       width;
    int       height;
} fb_t;

/* Open /dev/fb0. Returns 0 on success. */
int  fb_open(fb_t *fb);

/* Close and unmap. */
void fb_close(fb_t *fb);

/* Fill the entire framebuffer with color. */
void fb_clear(fb_t *fb, uint16_t color);

/* Draw a filled rectangle. */
void fb_fill_rect(fb_t *fb, int x, int y, int w, int h, uint16_t color);

/* Draw a rectangle outline. */
void fb_draw_rect(fb_t *fb, int x, int y, int w, int h, uint16_t color, int thickness);

/* Draw a horizontal line. */
void fb_hline(fb_t *fb, int x, int y, int len, uint16_t color);

/* Draw a vertical line. */
void fb_vline(fb_t *fb, int x, int y, int len, uint16_t color);

/* Blit an RGB565 pixel buffer into the framebuffer at (x, y). */
void fb_blit(fb_t *fb, int x, int y, int w, int h, const uint16_t *pixels);

/* Convenience: full-screen GC16 refresh. */
void fb_refresh_full(fb_t *fb);

/* Convenience: partial DU refresh for a region. */
void fb_refresh_partial(fb_t *fb, int x, int y, int w, int h);

/* Set a single pixel. Bounds-checked. */
static inline void fb_set_pixel(fb_t *fb, int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) return;
    fb->mem[y * fb->width + x] = color;
}

#endif /* SKEETS_FB_H */
