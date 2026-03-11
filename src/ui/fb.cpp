#include "fb.h"

#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <fbink.h>

typedef struct {
    const char *filename;
    FONT_STYLE_T style;
    const char *label;
    bool required;
} font_candidate_t;

static bool has_font_extension(const char *name) {
    if (!name) return false;
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    return strcasecmp(ext, ".ttf") == 0 ||
           strcasecmp(ext, ".otf") == 0 ||
           strcasecmp(ext, ".ttc") == 0;
}

static bool looks_like_text_font(const char *name) {
    if (!name || !*name) return false;
    return strstr(name, "Emoji") == NULL &&
           strstr(name, "emoji") == NULL &&
           strstr(name, "Color") == NULL &&
           strstr(name, "color") == NULL;
}

static bool is_known_font_name(const char *name, const font_candidate_t *candidates, size_t count) {
    if (!name) return false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(name, candidates[i].filename) == 0) return true;
    }
    return false;
}

/* ── Open / close ─────────────────────────────────────────────────── */

int fb_open(fb_t *fb) {
    if (!fb) return -1;
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;

    fprintf(stderr, "fb_open: opening FBInk framebuffer\n");
    fb->fd = fbink_open();
    if (fb->fd < 0) {
        fprintf(stderr, "fb_open: fbink_open failed\n");
        return -1;
    }

    FBInkConfig cfg = {};
    cfg.is_quiet = true;
    if (fbink_init(fb->fd, &cfg) < 0) {
        fprintf(stderr, "fb_open: fbink_init failed\n");
        fbink_close(fb->fd);
        fb->fd = -1;
        return -1;
    }

    FBInkState state = {};
    fbink_get_state(&cfg, &state);
    fb->width  = (int)state.screen_width;
    fb->height = (int)state.screen_height;
    fprintf(stderr, "fb_open: screen_width=%d screen_height=%d view_width=%u view_height=%u\n",
            fb->width, fb->height, state.view_width, state.view_height);

    return 0;
}

void fb_close(fb_t *fb) {
    if (!fb) return;
    fprintf(stderr, "fb_close: releasing FBInk resources\n");
    fbink_free_ot_fonts();
    if (fb->fd >= 0) { fbink_close(fb->fd); fb->fd = -1; }
}

int fb_load_fonts(fb_t *fb, const char *font_dir) {
    if (!fb || fb->fd < 0 || !font_dir) return -1;

    fprintf(stderr, "fb_load_fonts: loading fonts from %s\n", font_dir);
    static const font_candidate_t candidates[] = {
        {"NotoSans-Regular.ttf", FNT_REGULAR, "regular", true},
        {"NotoSans-Bold.ttf", FNT_BOLD, "bold", false},
        {"NotoSans-Italic.ttf", FNT_ITALIC, "italic", false},
        {"Caecilia_LT_65_Medium.ttf", FNT_REGULAR, "regular", true},
        {"Caecilia_LT_75_Bold.ttf", FNT_BOLD, "bold", false},
        {"Caecilia_LT_56_Italic.ttf", FNT_ITALIC, "italic", false},
    };

    char path[512];
    bool regular_loaded = false;
    bool bold_loaded = false;
    bool italic_loaded = false;

    for (const font_candidate_t &candidate : candidates) {
        if ((candidate.style == FNT_REGULAR && regular_loaded) ||
            (candidate.style == FNT_BOLD && bold_loaded) ||
            (candidate.style == FNT_ITALIC && italic_loaded)) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", font_dir, candidate.filename);
        if (fbink_add_ot_font(path, candidate.style) < 0) {
            if (candidate.required)
                fprintf(stderr, "fb_load_fonts: failed to load %s font: %s\n", candidate.label, path);
            continue;
        }

        fprintf(stderr, "fb_load_fonts: loaded %s font: %s\n", candidate.label, path);
        if (candidate.style == FNT_REGULAR) regular_loaded = true;
        if (candidate.style == FNT_BOLD) bold_loaded = true;
        if (candidate.style == FNT_ITALIC) italic_loaded = true;
    }

    if (!regular_loaded) {
        DIR *dir = opendir(font_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (!has_font_extension(entry->d_name) ||
                    is_known_font_name(entry->d_name, candidates, sizeof(candidates) / sizeof(candidates[0])) ||
                    !looks_like_text_font(entry->d_name)) {
                    continue;
                }

                snprintf(path, sizeof(path), "%s/%s", font_dir, entry->d_name);
                if (fbink_add_ot_font(path, FNT_REGULAR) < 0) continue;

                fprintf(stderr, "fb_load_fonts: loaded fallback regular font: %s\n", path);
                regular_loaded = true;
                break;
            }
            closedir(dir);
        }
    }

    if (!regular_loaded) {
        fprintf(stderr, "fb_load_fonts: no usable regular font found in %s\n", font_dir);
        return -1;
    }

    return 0;
}

/* ── Helpers ───────────────────────────────────────────────────────── */

/* Map an RGB565 color to 8-bit grayscale luminance. */
static uint8_t rgb565_to_gray(uint16_t c) {
    uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
    uint8_t b = (uint8_t)((c & 0x1F) << 3);
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

/* ── Drawing primitives ───────────────────────────────────────────── */

void fb_clear(fb_t *fb, uint16_t color) {
    if (!fb || fb->fd < 0) return;
    FBInkConfig cfg = {};
    cfg.no_refresh = true;
    cfg.bg_color = rgb565_to_gray(color);
    fbink_cls(fb->fd, &cfg, NULL, false);
}

void fb_fill_rect(fb_t *fb, int x, int y, int w, int h, uint16_t color) {
    if (!fb || fb->fd < 0 || w <= 0 || h <= 0) return;
    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb->width)  w = fb->width  - x;
    if (y + h > fb->height) h = fb->height - y;
    if (w <= 0 || h <= 0) return;

    FBInkConfig cfg = {};
    cfg.no_refresh = true;

    FBInkRect rect = {};
    rect.left   = (unsigned short int)x;
    rect.top    = (unsigned short int)y;
    rect.width  = (unsigned short int)w;
    rect.height = (unsigned short int)h;

    fbink_fill_rect_gray(fb->fd, &cfg, &rect, false, rgb565_to_gray(color));
}

void fb_draw_rect(fb_t *fb, int x, int y, int w, int h, uint16_t color, int thickness) {
    if (thickness <= 0) thickness = 1;
    fb_fill_rect(fb, x,             y,             w,         thickness, color); /* top */
    fb_fill_rect(fb, x,             y + h - thickness, w,     thickness, color); /* bottom */
    fb_fill_rect(fb, x,             y,             thickness, h,         color); /* left */
    fb_fill_rect(fb, x + w - thickness, y,         thickness, h,         color); /* right */
}

void fb_hline(fb_t *fb, int x, int y, int len, uint16_t color) {
    fb_fill_rect(fb, x, y, len, 1, color);
}

void fb_vline(fb_t *fb, int x, int y, int len, uint16_t color) {
    fb_fill_rect(fb, x, y, 1, len, color);
}

void fb_blit_rgba(fb_t *fb, int x, int y, int w, int h, const uint8_t *rgba) {
    if (!fb || fb->fd < 0 || !rgba || w <= 0 || h <= 0) return;

    FBInkConfig cfg = {};
    cfg.no_refresh = true;
    cfg.ignore_alpha = false;

    fbink_print_raw_data(fb->fd, rgba, w, h,
                         (size_t)(w * h * 4),
                         (short int)x, (short int)y, &cfg);
}

/* ── E-ink refresh (via FBInk) ────────────────────────────────────── */

void fb_refresh_full(fb_t *fb) {
    if (!fb || fb->fd < 0) return;
    static bool logged = false;
    FBInkConfig cfg = {};
    cfg.is_flashing = true;
    /* Empty region (0×0 @ 0,0) triggers a full-screen refresh. */
    if (!logged) {
        fprintf(stderr, "fb_refresh_full: first full refresh\n");
        logged = true;
    }
    fbink_refresh(fb->fd, 0, 0, 0, 0, &cfg);
}

void fb_refresh_partial(fb_t *fb, int x, int y, int w, int h) {
    if (!fb || fb->fd < 0) return;
    static bool logged = false;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    FBInkConfig cfg = {};
    cfg.wfm_mode = WFM_DU;
    if (!logged) {
        fprintf(stderr, "fb_refresh_partial: first partial refresh region=%d,%d %dx%d\n", x, y, w, h);
        logged = true;
    }
    fbink_refresh(fb->fd, (uint32_t)y, (uint32_t)x, (uint32_t)w, (uint32_t)h, &cfg);
}

void fb_refresh_fast(fb_t *fb, int x, int y, int w, int h) {
    if (!fb || fb->fd < 0) return;
    static bool logged = false;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    FBInkConfig cfg = {};
    cfg.wfm_mode = WFM_A2;
    if (!logged) {
        fprintf(stderr, "fb_refresh_fast: first fast refresh region=%d,%d %dx%d\n", x, y, w, h);
        logged = true;
    }
    fbink_refresh(fb->fd, (uint32_t)y, (uint32_t)x, (uint32_t)w, (uint32_t)h, &cfg);
}
