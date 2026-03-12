#ifndef SKEETS_FB_H
#define SKEETS_FB_H

#include <stdint.h>
#include <stdbool.h>

/* ── Colors (8-bit grayscale) ─────────────────────────────────────── */
#define COLOR_BLACK  ((uint8_t)0x00)
#define COLOR_WHITE  ((uint8_t)0xFF)
#define COLOR_GRAY   ((uint8_t)0x80)
#define COLOR_LGRAY  ((uint8_t)0xC0)
#define COLOR_DGRAY  ((uint8_t)0x40)

/* ── Framebuffer context ─────────────────────────────────────────── */
typedef struct {
    int       fd;         /* FBInk file descriptor */
    int       width;
    int       height;
    int       font_w;
    int       font_h;
} fb_t;

/* Open framebuffer via FBInk. Returns 0 on success. */
int  fb_open(fb_t *fb);

/* Close framebuffer. */
void fb_close(fb_t *fb);

/* Load OpenType fonts for text rendering. Call after fb_open().
 * font_dir: path to directory containing NotoSans-*.ttf files. */
int  fb_load_fonts(fb_t *fb, const char *font_dir);

/* Fill the entire framebuffer with color. */
void fb_clear(fb_t *fb, uint8_t color);

/* Draw a filled rectangle. */
void fb_fill_rect(fb_t *fb, int x, int y, int w, int h, uint8_t color);

/* Draw a rectangle outline. */
void fb_draw_rect(fb_t *fb, int x, int y, int w, int h, uint8_t color, int thickness);

/* Draw a horizontal line. */
void fb_hline(fb_t *fb, int x, int y, int len, uint8_t color);

/* Draw a vertical line. */
void fb_vline(fb_t *fb, int x, int y, int len, uint8_t color);

/* Blit an RGBA pixel buffer via FBInk (handles format conversion). */
void fb_blit_rgba(fb_t *fb, int x, int y, int w, int h, const uint8_t *rgba);

/* Convenience: full-screen GC16 refresh. */
void fb_refresh_full(fb_t *fb);

/* Partial DU refresh for a region (fast, monochrome). */
void fb_refresh_partial(fb_t *fb, int x, int y, int w, int h);

/* Fast A2 refresh for a region (very fast, binary only). */
void fb_refresh_fast(fb_t *fb, int x, int y, int w, int h);

/* Mark a region dirty for deferred refresh. */
void fb_mark_dirty(fb_t *fb, int x, int y, int w, int h);

/* Flush all dirty regions with a single refresh. */
void fb_flush(fb_t *fb);

/* Wait for any pending e-ink refresh to complete. */
void fb_wait_for_complete(fb_t *fb);

/* GC16 partial refresh for image regions (grayscale fidelity). */
void fb_refresh_gc16_partial(fb_t *fb, int x, int y, int w, int h);

#endif /* SKEETS_FB_H */
