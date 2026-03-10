#ifndef SKEETS_IMAGE_H
#define SKEETS_IMAGE_H

#include <stdint.h>

#define IMAGE_CACHE_DIR "/mnt/onboard/.adds/sKeets/cache/"

/*
 * RGBA pixel buffer returned by image_load_*.
 * Caller is responsible for calling image_free().
 */
typedef struct {
    uint8_t *pixels; /* RGBA row-major */
    int      width;
    int      height;
} image_t;

/*
 * Download an image from url and decode it.
 * Returns 0 on success and fills *out.
 * The pixel buffer is heap-allocated; call image_free() when done.
 */
int image_load_url(const char *url, image_t *out);

/*
 * Decode a local file path.
 */
int image_load_file(const char *path, image_t *out);

/* Scale image to fit within max_w x max_h, maintaining aspect ratio.
 * Replaces img->pixels with a new allocation. */
int image_scale_to_fit(image_t *img, int max_w, int max_h);

/* Convert RGBA to RGB565 for direct framebuffer blit.
 * dst must have at least (img->width * img->height * 2) bytes. */
void image_to_rgb565(const image_t *img, uint16_t *dst);

/* Free pixel data. */
void image_free(image_t *img);

/* Build the cache file path for a URL (URL hash-based). */
void image_cache_path(const char *url, char *out_path, int out_size);

#endif /* SKEETS_IMAGE_H */
