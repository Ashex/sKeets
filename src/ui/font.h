#ifndef SKEETS_FONT_H
#define SKEETS_FONT_H

#include "fb.h"
#include <stddef.h>

/* ── Glyph metrics ───────────────────────────────────────────────── */
#define FONT_CHAR_W   8   /* pixels per character width */
#define FONT_CHAR_H  16   /* pixels per character height */
#define FONT_FIRST   0x20 /* first renderable ASCII codepoint */
#define FONT_LAST    0x7E /* last  renderable ASCII codepoint */

/* ── Text drawing ────────────────────────────────────────────────── */

/*
 * Draw a single ASCII character at (x, y).
 * Returns the x-advance (FONT_CHAR_W).
 */
int font_draw_char(fb_t *fb, int x, int y, char c,
                   uint16_t fg, uint16_t bg);

/*
 * Draw a null-terminated string at (x, y).
 * Returns the x position after the last character.
 */
int font_draw_string(fb_t *fb, int x, int y, const char *s,
                     uint16_t fg, uint16_t bg);

/*
 * Draw a string with word-wrap within a bounding box [x, x+max_w).
 * Returns the y-position after the last line.
 * line_spacing: extra pixels between lines (0 = tight).
 */
int font_draw_wrapped(fb_t *fb, int x, int y, int max_w,
                      const char *s, uint16_t fg, uint16_t bg,
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
