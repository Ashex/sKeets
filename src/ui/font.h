#ifndef SKEETS_FONT_H
#define SKEETS_FONT_H

#include "fb.h"
#include <stddef.h>

/* ── Glyph metrics (approximate, for layout calculations) ────────── */
#define FONT_FIRST   0x20
#define FONT_LAST    0x7E

/* ── Init / shutdown ─────────────────────────────────────────────── */

/* Store FBInk fd for font measurement functions. Call after fb_load_fonts(). */
void font_init(fb_t *fb);

/* Enable or disable OpenType rendering. Disabled mode falls back to FBInk's
 * built-in bitmap font support. */
void font_set_ot_enabled(bool enabled);

/* Returns current font cell width in pixels. */
int font_cell_w(void);

/* Returns current font cell height in pixels. */
int font_cell_h(void);

/* ── Text drawing ────────────────────────────────────────────────── */

/*
 * Draw a single ASCII character at (x, y).
 * Returns the x-advance (font_cell_w()).
 */
int font_draw_char(fb_t *fb, int x, int y, char c,
                   uint8_t fg, uint8_t bg);

/*
 * Draw a null-terminated string at (x, y).
 * Returns the x position after the last character.
 */
int font_draw_string(fb_t *fb, int x, int y, const char *s,
                     uint8_t fg, uint8_t bg);

/*
 * Draw a string with word-wrap within a bounding box [x, x+max_w).
 * Returns the y-position after the last line.
 * line_spacing: extra pixels between lines (0 = tight).
 */
int font_draw_wrapped(fb_t *fb, int x, int y, int max_w,
                      const char *s, uint8_t fg, uint8_t bg,
                      int line_spacing);

/*
 * Measure wrapped text height (in pixels) without drawing.
 */
int font_measure_wrapped(int max_w, const char *s, int line_spacing);

/*
 * Measure the width of a single line of text (no wrap).
 */
int font_measure_string(const char *s);

#endif /* SKEETS_FONT_H */
