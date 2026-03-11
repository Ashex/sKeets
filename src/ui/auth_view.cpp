#include "auth_view.h"
#include "fb.h"
#include "font.h"
#include "../util/paths.h"
#include "../util/str.h"

#include <string.h>
#include <stdio.h>

#define EXIT_BTN_X  16
#define EXIT_BTN_Y  16
#define EXIT_BTN_W  96
#define EXIT_BTN_H  40

static char s_error[256];

static void draw_exit_button(fb_t *fb) {
    fb_fill_rect(fb, EXIT_BTN_X, EXIT_BTN_Y, EXIT_BTN_W, EXIT_BTN_H, COLOR_DGRAY);
    const char *label = "Exit";
    int lw = font_measure_string(label);
    font_draw_string(fb, EXIT_BTN_X + (EXIT_BTN_W - lw) / 2,
                     EXIT_BTN_Y + (EXIT_BTN_H - font_cell_h()) / 2,
                     label, COLOR_WHITE, COLOR_DGRAY);
}

void auth_view_draw(app_state_t *state) {
    fb_t *fb = &state->fb;
    int fb_w = fb->width;
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

    if (s_error[0]) {
        y += 16;
        int ew = font_measure_string(s_error);
        font_draw_string(fb, (fb_w - ew) / 2, y, s_error, COLOR_BLACK, COLOR_WHITE);
    }

    fb_refresh_full(fb);
}

void auth_view_handle(app_state_t *state, const input_event_t *ev) {
    if (ev->type != INPUT_TOUCH || ev->touch.type != TOUCH_UP) return;
    int tx = ev->touch.x;
    int ty = ev->touch.y;
    if (hit_test_rect(tx, ty, EXIT_BTN_X, EXIT_BTN_Y, EXIT_BTN_W, EXIT_BTN_H)) {
        state->running = false;
    }
}

void auth_view_set_error(const char *msg) {
    if (msg) str_safe_copy(s_error, msg, sizeof(s_error));
    else s_error[0] = '\0';
}
