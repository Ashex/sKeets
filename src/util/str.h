#ifndef SKEETS_STR_H
#define SKEETS_STR_H

#include <stddef.h>
#include <stdbool.h>

/* Safe string copy - always null-terminates dst */
void str_safe_copy(char *dst, const char *src, size_t dst_size);

/* Trim leading/trailing whitespace in-place */
void str_trim(char *s);

/* URL-encode src into dst; returns bytes written (excluding null) */
int str_url_encode(char *dst, size_t dst_size, const char *src);

/* djb2a (XOR variant) hash of a string */
unsigned long str_hash(const char *s);

/* Check if string starts with prefix */
bool str_starts_with(const char *s, const char *prefix);

/* Check if string is empty or NULL */
bool str_empty(const char *s);

/* Append src to dst safely; dst_size is total buffer size */
void str_safe_append(char *dst, const char *src, size_t dst_size);

/* Format a UTC time_t as "HH:MM · MMM DD" for display */
void str_format_time(char *buf, size_t buf_size, long epoch_secs);

/* Convert UTF-8 byte length to display column width (approximate) */
int str_utf8_display_len(const char *s, int max_bytes);

#endif /* SKEETS_STR_H */
