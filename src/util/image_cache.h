#ifndef SKEETS_IMAGE_CACHE_H
#define SKEETS_IMAGE_CACHE_H

#include "image.h"

/*
 * Asynchronous, non-blocking image cache.
 *
 * On first lookup the image is fetched in the background via Qt async
 * networking.  While loading, the lookup returns nullptr.  Once the
 * download completes and the image is decoded + pre-scaled, subsequent
 * lookups return a pointer to the ready image_t.
 *
 * A global "redraw needed" flag is set whenever a new image becomes
 * ready so the main loop can trigger a repaint.
 */

/* Look up a URL in the in-memory cache.
 *   - Returns a pointer to the ready (pre-scaled) image, or nullptr if
 *     the image is not yet available (still loading or not requested).
 *   - If the URL has not been requested yet, an async download is
 *     kicked off automatically.
 *   - scale_w / scale_h: maximum dimensions for pre-scaling on insert.
 *     Pass 0,0 to store at original resolution. */
const image_t *image_cache_lookup(const char *url, int scale_w, int scale_h);

/* Returns true (and clears the flag) when at least one new image has
 * become ready since the last call. */
bool image_cache_redraw_needed();

/* Discard all in-flight requests and cached images. */
void image_cache_clear();

#endif /* SKEETS_IMAGE_CACHE_H */
