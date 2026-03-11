#ifndef SKEETS_FB_H
#define SKEETS_FB_H

#include <stdint.h>
#include <stdbool.h>

/* ── Display constants ───────────────────────────────────────────── */
#define FB_WIDTH    1072
#define FB_HEIGHT   1448


/* ── Colors (RGB565) ─────────────────────────────────────────────── */
#define COLOR_BLACK  ((uint16_t)0x0000)
#define COLOR_WHITE  ((uint16_t)0xFFFF)
#define COLOR_GRAY   ((uint16_t)0x8410)  /* mid-gray ~128,128,128 */
#define COLOR_LGRAY  ((uint16_t)0xC618)  /* light gray */
#define COLOR_DGRAY  ((uint16_t)0x4208)  /* dark gray */

/* ── Framebuffer context ─────────────────────────────────────────── */
typedef struct {
    int       fd;         /* FBInk file descriptor */
    int       width;
    int       height;
} fb_t;

/* Open framebuffer via FBInk. Returns 0 on success. */
int  fb_open(fb_t *fb);

/* Close framebuffer. */
void fb_close(fb_t *fb);

/* Load OpenType fonts for text rendering. Call after fb_open().
 * font_dir: path to directory containing NotoSans-*.ttf files. */
int  fb_load_fonts(fb_t *fb, const char *font_dir);

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

/* Blit an RGBA pixel buffer via FBInk (handles format conversion). */
void fb_blit_rgba(fb_t *fb, int x, int y, int w, int h, const uint8_t *rgba);

/* Convenience: full-screen GC16 refresh. */
void fb_refresh_full(fb_t *fb);

/* Partial DU refresh for a region (fast, monochrome). */
void fb_refresh_partial(fb_t *fb, int x, int y, int w, int h);

/* Fast A2 refresh for a region (very fast, binary only). */
void fb_refresh_fast(fb_t *fb, int x, int y, int w, int h);

#endif /* SKEETS_FB_H */
