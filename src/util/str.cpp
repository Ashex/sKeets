#include "str.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

void str_safe_copy(char *dst, const char *src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i;
    for (i = 0; i < dst_size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void str_safe_append(char *dst, const char *src, size_t dst_size) {
    if (!dst || dst_size == 0 || !src) return;
    size_t cur = strlen(dst);
    if (cur >= dst_size - 1) return;
    str_safe_copy(dst + cur, src, dst_size - cur);
}

void str_trim(char *s) {
    if (!s) return;
    /* Trim trailing */
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    /* Trim leading */
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
}

int str_url_encode(char *dst, size_t dst_size, const char *src) {
    static const char hex[] = "0123456789ABCDEF";
    size_t written = 0;
    if (!dst || dst_size == 0 || !src) return 0;
    while (*src && written + 4 < dst_size) {
        unsigned char c = (unsigned char)*src;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[written++] = (char)c;
        } else {
            dst[written++] = '%';
            dst[written++] = hex[(c >> 4) & 0xF];
            dst[written++] = hex[c & 0xF];
        }
        src++;
    }
    dst[written] = '\0';
    return (int)written;
}

/* djb2a: h = h * 33 ^ c */
unsigned long str_hash(const char *s) {
    unsigned long h = 5381;
    int c;
    if (!s) return 0;
    while ((c = (unsigned char)*s++))
        h = ((h << 5) + h) ^ (unsigned long)c;
    return h;
}

bool str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool str_empty(const char *s) {
    return !s || s[0] == '\0';
}

void str_format_time(char *buf, size_t buf_size, long epoch_secs) {
    static const char *months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    time_t t = (time_t)epoch_secs;
    struct tm *tm = gmtime(&t);
    if (!tm) {
        str_safe_copy(buf, "??:?? · ??? ??", buf_size);
        return;
    }
    snprintf(buf, buf_size, "%02d:%02d · %s %d",
             tm->tm_hour, tm->tm_min,
             months[tm->tm_mon], tm->tm_mday);
}

int str_utf8_display_len(const char *s, int max_bytes) {
    if (!s) return 0;
    int len = 0, i = 0;
    while (i < max_bytes && s[i]) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80)       { i += 1; len += 1; }
        else if (c < 0xE0)  { i += 2; len += 1; }
        else if (c < 0xF0)  { i += 3; len += 1; } /* CJK wide */
        else                { i += 4; len += 2; } /* surrogate-range / emoji wide */
    }
    return len;
}
