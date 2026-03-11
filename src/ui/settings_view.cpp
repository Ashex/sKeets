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
#define ROW_W        (FB_WIDTH - UI_MARGIN * 2)
#define ROW_H        64
#define TOGGLE_W     80
#define TOGGLE_H     36
#define ROW_Y_START  (UI_BAR_H + 24)
#define ROW_SPACING  8
#define LOGOUT_BTN_W 160
#define LOGOUT_BTN_H 40

/* Y position of the logout button row. */
static inline int logout_row_y(void) {
    return FB_HEIGHT - UI_BAR_H - ROW_H - ROW_SPACING;
}

/* Top-left of the logout button itself, centred within its row. */
static inline int logout_btn_x(void) { return ROW_X + (ROW_W - LOGOUT_BTN_W) / 2; }
static inline int logout_btn_y(void) { return logout_row_y() + (ROW_H - LOGOUT_BTN_H) / 2; }

typedef struct {
    const char *label;
    const char *description;
    bool       *value;
} setting_row_t;

static void draw_toggle(fb_t *fb, int x, int y, int w, int h, bool on) {
    uint16_t bg = on ? COLOR_BLACK : COLOR_LGRAY;
    fb_fill_rect(fb, x, y, w, h, bg);
    int knob_size = h - 8;
    int knob_x    = on ? (x + w - knob_size - 4) : (x + 4);
    fb_fill_rect(fb, knob_x, y + 4, knob_size, knob_size, COLOR_WHITE);
    const char *state_label = on ? "ON" : "OFF";
    int label_w = font_measure_string(state_label);
    int label_x = on ? (x + 4) : (x + w - label_w - 4);
    font_draw_string(fb, label_x, y + (h - FONT_CHAR_H) / 2, state_label, COLOR_WHITE, bg);
}

static void draw_setting_row(fb_t *fb, int y, const setting_row_t *row) {
    fb_fill_rect(fb, ROW_X, y, ROW_W, ROW_H, COLOR_WHITE);
    font_draw_string(fb, ROW_X, y + 10, row->label, COLOR_BLACK, COLOR_WHITE);
    font_draw_string(fb, ROW_X, y + 10 + FONT_CHAR_H + 4,
                     row->description, COLOR_GRAY, COLOR_WHITE);
    int toggle_x = ROW_X + ROW_W - TOGGLE_W - 8;
    int toggle_y = y + (ROW_H - TOGGLE_H) / 2;
    draw_toggle(fb, toggle_x, toggle_y, TOGGLE_W, TOGGLE_H, *row->value);
    fb_hline(fb, ROW_X, y + ROW_H - 1, ROW_W, COLOR_LGRAY);
}

static void draw_topbar(fb_t *fb) {
    fb_fill_rect(fb, 0, 0, FB_WIDTH, UI_BAR_H, COLOR_WHITE);
    fb_hline(fb, 0, UI_BAR_H - 1, FB_WIDTH, COLOR_BLACK);
    font_draw_string(fb, UI_MARGIN, (UI_BAR_H - FONT_CHAR_H) / 2,
                     "< Back", COLOR_BLACK, COLOR_WHITE);
    const char *title = "Settings";
    int title_w = font_measure_string(title);
    font_draw_string(fb, (FB_WIDTH - title_w) / 2, (UI_BAR_H - FONT_CHAR_H) / 2,
                     title, COLOR_BLACK, COLOR_WHITE);
}

static void draw_logout_button(fb_t *fb) {
    int row_y = logout_row_y();
    fb_fill_rect(fb, ROW_X, row_y, ROW_W, ROW_H, COLOR_WHITE);
    int bx = logout_btn_x();
    int by = logout_btn_y();
    fb_draw_rect(fb, bx, by, LOGOUT_BTN_W, LOGOUT_BTN_H, COLOR_BLACK, 2);
    const char *label = "Sign Out";
    int label_w = font_measure_string(label);
    font_draw_string(fb, bx + (LOGOUT_BTN_W - label_w) / 2,
                     by + (LOGOUT_BTN_H - FONT_CHAR_H) / 2,
                     label, COLOR_BLACK, COLOR_WHITE);
}

static void draw_version(fb_t *fb) {
    const char *version = "sKeets v1.0.0  |  ATProto/Bluesky";
    int version_w = font_measure_string(version);
    font_draw_string(fb, (FB_WIDTH - version_w) / 2,
                     FB_HEIGHT - UI_BAR_H / 2 - FONT_CHAR_H / 2,
                     version, COLOR_GRAY, COLOR_WHITE);
}

void settings_view_draw(app_state_t *state) {
    fb_t *fb = &state->fb;
    fb_clear(fb, COLOR_WHITE);
    draw_topbar(fb);

    setting_row_t rows[] = {
        { "Show Images", "Load & display profile photos and post images", &state->images_enabled },
    };
    int n_rows = (int)(sizeof(rows) / sizeof(rows[0]));
    int y = ROW_Y_START;
    for (int i = 0; i < n_rows; i++) {
        draw_setting_row(fb, y, &rows[i]);
        y += ROW_H + ROW_SPACING;
    }

    draw_logout_button(fb);
    draw_version(fb);
    fb_refresh_full(fb);
}

void settings_view_handle(app_state_t *state, const input_event_t *ev) {
    if (ev->type != INPUT_TOUCH || ev->touch.type != TOUCH_UP) return;
    int tx = ev->touch.x;
    int ty = ev->touch.y;

    if (ty < UI_BAR_H && tx < 120) {
        settings_save(state);
        app_switch_view(state, VIEW_FEED);
        return;
    }

    setting_row_t rows[] = {
        { nullptr, nullptr, &state->images_enabled },
    };
    int n_rows = (int)(sizeof(rows) / sizeof(rows[0]));
    int y = ROW_Y_START;
    for (int i = 0; i < n_rows; i++) {
        if (hit_test_rect(tx, ty, ROW_X, y, ROW_W, ROW_H)) {
            *rows[i].value = !*rows[i].value;
            settings_save(state);
            settings_view_draw(state);
            return;
        }
        y += ROW_H + ROW_SPACING;
    }

    if (hit_test_rect(tx, ty, logout_btn_x(), logout_btn_y(), LOGOUT_BTN_W, LOGOUT_BTN_H)) {
        state->session = Bsky::Session{};
        config_t *cfg = config_open(skeets_config_path());
        if (cfg) {
            config_set_str(cfg, "access_jwt",  "");
            config_set_str(cfg, "refresh_jwt", "");
            config_save(cfg);
            config_free(cfg);
        }
        app_switch_view(state, VIEW_LOGIN);
    }
}

void settings_save(const app_state_t *state) {
    config_t *cfg = config_open(skeets_config_path());
    if (!cfg) return;
    config_set_bool(cfg, "images_enabled", state->images_enabled);
    config_save(cfg);
    config_free(cfg);
}
