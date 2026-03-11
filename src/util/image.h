#ifndef SKEETS_IMAGE_H
#define SKEETS_IMAGE_H

#include <stdint.h>

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

/* Free pixel data. */
void image_free(image_t *img);

/* Build the cache file path for a URL (URL hash-based). */
void image_cache_path(const char *url, char *out_path, int out_size);

/* Decode raw RGBA bytes from a QByteArray (JPEG/PNG/etc.).
 * Returns 0 on success, fills *out. Caller must image_free(). */
int image_decode_memory(const uint8_t *data, int len, image_t *out);

/* Write image to disk cache for the given URL. */
void image_write_disk_cache(const char *url, const image_t *img);

#endif /* SKEETS_IMAGE_H */
