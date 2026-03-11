#include "auth_view.h"
#include "fb.h"
#include "font.h"
#include "../util/paths.h"
#include "../util/str.h"

#include <string.h>
#include <stdio.h>

#define EXIT_BTN_INSET  (UI_MARGIN + 8)
#define EXIT_BTN_W      112
#define EXIT_BTN_H      48
#define ERROR_BOX_W     640
#define ERROR_BOX_PAD   24

static char s_error[256];
static char s_info[256];
static bool s_exit_tracking;
static touch_event_t s_exit_down;

static inline int exit_btn_x(void) {
    return EXIT_BTN_INSET;
}

static inline int exit_btn_y(void) {
    return EXIT_BTN_INSET;
}

static inline int exit_hit_x(void) {
    return exit_btn_x() - UI_PAD;
}

static inline int exit_hit_y(void) {
    return exit_btn_y() - UI_PAD;
}

static inline int exit_hit_w(void) {
    return EXIT_BTN_W + UI_PAD * 2;
}

static inline int exit_hit_h(void) {
    return EXIT_BTN_H + UI_PAD * 2;
}

static void draw_exit_button(fb_t *fb) {
    int x = exit_btn_x();
    int y = exit_btn_y();
    fb_fill_rect(fb, x, y, EXIT_BTN_W, EXIT_BTN_H, COLOR_DGRAY);
    fb_draw_rect(fb, x, y, EXIT_BTN_W, EXIT_BTN_H, COLOR_BLACK, 2);
    const char *label = "Exit";
    int lw = font_measure_string(label);
    font_draw_string(fb, x + (EXIT_BTN_W - lw) / 2,
                     y + (EXIT_BTN_H - font_cell_h()) / 2,
                     label, COLOR_WHITE, COLOR_DGRAY);
}

static void draw_error_box(fb_t *fb, int fb_w, int fb_h, int font_h, const char *msg) {
    if (!msg || !msg[0]) return;

    int box_w = ERROR_BOX_W;
    if (box_w > fb_w - UI_MARGIN * 4)
        box_w = fb_w - UI_MARGIN * 4;
    int text_w = box_w - ERROR_BOX_PAD * 2;
    int text_h = font_measure_wrapped(text_w, msg, 2);
    int title_h = font_h;
    int box_h = ERROR_BOX_PAD * 2 + title_h + 12 + text_h;
    int box_x = (fb_w - box_w) / 2;
    int box_y = (fb_h - box_h) / 2;

    fb_fill_rect(fb, box_x, box_y, box_w, box_h, COLOR_WHITE);
    fb_draw_rect(fb, box_x, box_y, box_w, box_h, COLOR_BLACK, 2);

    const char *title = "Error";
    int title_w = font_measure_string(title);
    font_draw_string(fb, box_x + (box_w - title_w) / 2,
                     box_y + ERROR_BOX_PAD,
                     title, COLOR_BLACK, COLOR_WHITE);
    font_draw_wrapped(fb, box_x + ERROR_BOX_PAD,
                      box_y + ERROR_BOX_PAD + title_h + 12,
                      text_w, msg, COLOR_BLACK, COLOR_WHITE, 2);
}

void auth_view_draw(app_state_t *state) {
    fb_t *fb = &state->fb;
    int fb_w = fb->width;
    int fb_h = fb->height;
    int font_h = font_cell_h();
    fb_clear(fb, COLOR_WHITE);
    draw_exit_button(fb);

    int y = 120;
    const char *title = "sKeets \xe2\x80\x94 Bluesky for Kobo";
    int tw = font_measure_string(title);
    font_draw_string(fb, (fb_w - tw) / 2, y, title, COLOR_BLACK, COLOR_WHITE);
    y += font_h + 16;

    const char *subtitle = "Sign in by creating a login.txt file:";
    int sw = font_measure_string(subtitle);
    font_draw_string(fb, (fb_w - sw) / 2, y, subtitle, COLOR_GRAY, COLOR_WHITE);
    y += font_h + 24;

    const char *lines[] = {
        "1. Connect Kobo via USB",
        "2. Create the file:",
        "       .adds/sKeets/login.txt",
        "3. Add these lines:",
        "       handle=yourhandle.bsky.social",
        "       password=your-app-password",
        "   (optional: pds_url=https://bsky.social)",
        "   (optional: appview=https://api.bsky.app)",
        "4. Eject USB, then restart sKeets",
        "",
        "Get an app password at:",
        "   bsky.app -> Settings -> App Passwords",
    };
    int mx = 80;
    int mw = fb_w - mx * 2;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        font_draw_wrapped(fb, mx, y, mw, lines[i], COLOR_BLACK, COLOR_WHITE, 2);
        y += font_h + 4;
    }

    if (s_info[0]) {
        int iw = font_measure_string(s_info);
        font_draw_string(fb, (fb_w - iw) / 2, y, s_info, COLOR_GRAY, COLOR_WHITE);
    }

    if (s_error[0])
        draw_error_box(fb, fb_w, fb_h, font_h, s_error);

    fb_refresh_full(fb);
}

void auth_view_handle(app_state_t *state, const input_event_t *ev) {
    if (ev->type != INPUT_TOUCH) return;

    int tx = ev->touch.x;
    int ty = ev->touch.y;
    switch (ev->touch.type) {
        case TOUCH_DOWN:
            s_exit_tracking = hit_test_rect(tx, ty, exit_hit_x(), exit_hit_y(), exit_hit_w(), exit_hit_h());
            if (s_exit_tracking)
                s_exit_down = ev->touch;
            break;
        case TOUCH_UP:
            if (s_exit_tracking && input_is_tap(&s_exit_down, &ev->touch))
                state->running = false;
            s_exit_tracking = false;
            break;
        default:
            if (ev->touch.type != TOUCH_MOVE)
                s_exit_tracking = false;
            break;
    }
}

void auth_view_set_error(const char *msg) {
    if (msg) str_safe_copy(s_error, msg, sizeof(s_error));
    else s_error[0] = '\0';
}

void auth_view_set_info(const char *msg) {
    if (msg) str_safe_copy(s_info, msg, sizeof(s_info));
    else s_info[0] = '\0';
}
