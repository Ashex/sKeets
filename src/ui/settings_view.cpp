#include "settings_view.h"
#include "fb.h"
#include "font.h"
#include "../util/config.h"
#include "../util/paths.h"
#include "../util/str.h"

#include <string.h>
#include <stdio.h>

/* ── Layout constants ─────────────────────────────────────────────── */
#define ROW_X        UI_MARGIN
#define ROW_H        64
#define TOGGLE_W     80
#define TOGGLE_H     36
#define ROW_Y_START  (UI_BAR_H + 24)
#define ROW_SPACING  8
#define LOGOUT_BTN_W 160
#define LOGOUT_BTN_H 40

static inline int row_w(int fb_w)        { return fb_w - UI_MARGIN * 2; }
static inline int logout_row_y(int fb_h) { return fb_h - UI_BAR_H - ROW_H - ROW_SPACING; }
static inline int logout_btn_x(int fb_w) { return ROW_X + (row_w(fb_w) - LOGOUT_BTN_W) / 2; }
static inline int logout_btn_y(int fb_h) { return logout_row_y(fb_h) + (ROW_H - LOGOUT_BTN_H) / 2; }

typedef struct {
    const char *label;
    const char *description;
    bool       *value;
} setting_row_t;

static void draw_toggle(fb_t *fb, int x, int y, int w, int h, bool on) {
    uint8_t bg = on ? COLOR_BLACK : COLOR_LGRAY;
    fb_fill_rect(fb, x, y, w, h, bg);
    int knob_size = h - 8;
    int knob_x    = on ? (x + w - knob_size - 4) : (x + 4);
    fb_fill_rect(fb, knob_x, y + 4, knob_size, knob_size, COLOR_WHITE);
    const char *state_label = on ? "ON" : "OFF";
    int label_w = font_measure_string(state_label);
    int label_x = on ? (x + 4) : (x + w - label_w - 4);
    font_draw_string(fb, label_x, y + (h - font_cell_h()) / 2, state_label, COLOR_WHITE, bg);
}

static void draw_setting_row(fb_t *fb, int y, const setting_row_t *row, int fb_w) {
    int rw = row_w(fb_w);
    int font_h = font_cell_h();
    fb_fill_rect(fb, ROW_X, y, rw, ROW_H, COLOR_WHITE);
    font_draw_string(fb, ROW_X, y + 10, row->label, COLOR_BLACK, COLOR_WHITE);
    font_draw_string(fb, ROW_X, y + 10 + font_h + 4,
                     row->description, COLOR_GRAY, COLOR_WHITE);
    int toggle_x = ROW_X + rw - TOGGLE_W - 8;
    int toggle_y = y + (ROW_H - TOGGLE_H) / 2;
    draw_toggle(fb, toggle_x, toggle_y, TOGGLE_W, TOGGLE_H, *row->value);
    fb_hline(fb, ROW_X, y + ROW_H - 1, rw, COLOR_LGRAY);
}

static void draw_topbar(fb_t *fb, int fb_w) {
    int font_h = font_cell_h();
    fb_fill_rect(fb, 0, 0, fb_w, UI_BAR_H, COLOR_WHITE);
    fb_hline(fb, 0, UI_BAR_H - 1, fb_w, COLOR_BLACK);
    font_draw_string(fb, UI_MARGIN, (UI_BAR_H - font_h) / 2,
                     "< Back", COLOR_BLACK, COLOR_WHITE);
    const char *title = "Settings";
    int title_w = font_measure_string(title);
    font_draw_string(fb, (fb_w - title_w) / 2, (UI_BAR_H - font_h) / 2,
                     title, COLOR_BLACK, COLOR_WHITE);
}

static void draw_logout_button(fb_t *fb, int fb_w, int fb_h) {
    int rw = row_w(fb_w);
    int row_y = logout_row_y(fb_h);
    int font_h = font_cell_h();
    fb_fill_rect(fb, ROW_X, row_y, rw, ROW_H, COLOR_WHITE);
    int bx = logout_btn_x(fb_w);
    int by = logout_btn_y(fb_h);
    fb_draw_rect(fb, bx, by, LOGOUT_BTN_W, LOGOUT_BTN_H, COLOR_BLACK, 2);
    const char *label = "Sign Out";
    int label_w = font_measure_string(label);
    font_draw_string(fb, bx + (LOGOUT_BTN_W - label_w) / 2,
                     by + (LOGOUT_BTN_H - font_h) / 2,
                     label, COLOR_BLACK, COLOR_WHITE);
}

static void draw_version(fb_t *fb, int fb_w, int fb_h) {
    int font_h = font_cell_h();
    const char *version = "sKeets v1.0.0  |  ATProto/Bluesky";
    int version_w = font_measure_string(version);
    font_draw_string(fb, (fb_w - version_w) / 2,
                     fb_h - UI_BAR_H / 2 - font_h / 2,
                     version, COLOR_GRAY, COLOR_WHITE);
}

void settings_view_draw(app_state_t *state) {
    fb_t *fb = &state->fb;
    int fb_w = fb->width;
    int fb_h = fb->height;
    fb_clear(fb, COLOR_WHITE);
    draw_topbar(fb, fb_w);

    setting_row_t rows[] = {
        { "Profile Images", "Load & display profile avatars", &state->profile_images_enabled },
        { "Embed Images",   "Load & display post images and external thumbnails", &state->embed_images_enabled },
    };
    int n_rows = (int)(sizeof(rows) / sizeof(rows[0]));
    int y = ROW_Y_START;
    for (int i = 0; i < n_rows; i++) {
        draw_setting_row(fb, y, &rows[i], fb_w);
        y += ROW_H + ROW_SPACING;
    }

    draw_logout_button(fb, fb_w, fb_h);
    draw_version(fb, fb_w, fb_h);
    fb_refresh_full(fb);
}

void settings_view_handle(app_state_t *state, const input_event_t *ev) {
    if (ev->type != INPUT_TOUCH || ev->touch.type != TOUCH_UP) return;
    int tx = ev->touch.x;
    int ty = ev->touch.y;
    int fb_w = state->fb.width;
    int fb_h = state->fb.height;

    if (ty < UI_BAR_H && tx < 120) {
        settings_save(state);
        app_switch_view(state, VIEW_FEED);
        return;
    }

    setting_row_t rows[] = {
        { nullptr, nullptr, &state->profile_images_enabled },
        { nullptr, nullptr, &state->embed_images_enabled },
    };
    int n_rows = (int)(sizeof(rows) / sizeof(rows[0]));
    int y = ROW_Y_START;
    int rw = row_w(fb_w);
    for (int i = 0; i < n_rows; i++) {
        if (hit_test_rect(tx, ty, ROW_X, y, rw, ROW_H)) {
            *rows[i].value = !*rows[i].value;
            settings_save(state);
            settings_view_draw(state);
            return;
        }
        y += ROW_H + ROW_SPACING;
    }

    if (hit_test_rect(tx, ty, logout_btn_x(fb_w), logout_btn_y(fb_h), LOGOUT_BTN_W, LOGOUT_BTN_H)) {
        state->session = Bsky::Session{};
        config_t *cfg = config_open(skeets_config_path());
        if (cfg) {
            config_set_str(cfg, "access_jwt",  "");
            config_set_str(cfg, "refresh_jwt", "");
            config_save(cfg);
            config_free(cfg);
        }
        app_switch_view(state, VIEW_AUTH_WAIT);
    }
}

void settings_save(const app_state_t *state) {
    config_t *cfg = config_open(skeets_config_path());
    if (!cfg) return;
    config_set_bool(cfg, "profile_images_enabled", state->profile_images_enabled);
    config_set_bool(cfg, "embed_images_enabled",   state->embed_images_enabled);
    config_save(cfg);
    config_free(cfg);
}
