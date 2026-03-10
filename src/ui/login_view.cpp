#include "login_view.h"
#include "fb.h"
#include "font.h"
#include "../atproto/atproto_client.h"
#include "../util/str.h"
#include "../util/config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Layout constants ─────────────────────────────────────────────── */
#define FIELD_X          (FB_WIDTH / 2 - 200)
#define FIELD_W          400
#define FIELD_H          48
#define HANDLE_Y         340
#define PASS_Y           (HANDLE_Y + FIELD_H + 24)
#define PDS_Y            (PASS_Y + FIELD_H + 24)
#define BTN_Y            (PDS_Y + FIELD_H + 40)
#define BTN_W            200
#define BTN_H            52
#define LOGIN_TITLE_Y    200
#define LOGIN_SUBTITLE_Y 240

/* Horizontal centre of the login button – computed once, used in draw & handle. */
static inline int login_btn_x(void) { return FB_WIDTH / 2 - BTN_W / 2; }

typedef enum { FOCUS_HANDLE = 0, FOCUS_PASSWORD = 1, FOCUS_PDS = 2, FOCUS_NONE = 3 } field_focus_t;

static char s_handle[128];
static char s_password[128];
static char s_pds_url[256];  /* Optional – blank means default bsky.social. */
static field_focus_t s_focus = FOCUS_HANDLE;
static char s_error[256];

/* ── Private drawing helpers ─────────────────────────────────────── */

/* Draw a single-line text input field with optional focus indicator. */
static void draw_text_field(fb_t *fb, int x, int y, int w, int h,
                             const char *label, const char *value, bool focused) {
    fb_fill_rect(fb, x, y, w, h, COLOR_WHITE);
    uint16_t border_color  = focused ? COLOR_BLACK : COLOR_GRAY;
    int      border_weight = focused ? 3 : 1;
    fb_draw_rect(fb, x, y, w, h, border_color, border_weight);
    font_draw_string(fb, x, y - FONT_CHAR_H - 4, label, COLOR_BLACK, COLOR_WHITE);

    int max_visible_chars = (w - 8) / FONT_CHAR_W;
    int value_len = (int)strlen(value);
    int start_char = value_len - max_visible_chars + 1;
    if (start_char < 0) start_char = 0;
    char display[128];
    str_safe_copy(display, value + start_char, sizeof(display));

    font_draw_string(fb, x + 8, y + (h - FONT_CHAR_H) / 2, display, COLOR_BLACK, COLOR_WHITE);
    if (focused) {
        int visible_len = (int)strlen(display);
        if (visible_len > max_visible_chars) visible_len = max_visible_chars;
        int cursor_x = x + 8 + visible_len * FONT_CHAR_W;
        fb_vline(fb, cursor_x, y + 8, h - 16, COLOR_BLACK);
    }
}

/* Draw a password field – text is masked as asterisks. */
static void draw_password_field(fb_t *fb, int x, int y, int w, int h,
                                 const char *label, const char *value, bool focused) {
    fb_fill_rect(fb, x, y, w, h, COLOR_WHITE);
    uint16_t border_color  = focused ? COLOR_BLACK : COLOR_GRAY;
    int      border_weight = focused ? 3 : 1;
    fb_draw_rect(fb, x, y, w, h, border_color, border_weight);
    font_draw_string(fb, x, y - FONT_CHAR_H - 4, label, COLOR_BLACK, COLOR_WHITE);

    int max_visible_chars = (w - 8) / FONT_CHAR_W;
    int value_len = (int)strlen(value);
    int masked_len = value_len < max_visible_chars ? value_len : max_visible_chars - 1;
    char masked[128] = "";
    for (int i = 0; i < masked_len; i++) masked[i] = '*';
    masked[masked_len] = '\0';

    font_draw_string(fb, x + 8, y + (h - FONT_CHAR_H) / 2, masked, COLOR_BLACK, COLOR_WHITE);
    if (focused) {
        int cursor_x = x + 8 + masked_len * FONT_CHAR_W;
        fb_vline(fb, cursor_x, y + 8, h - 16, COLOR_BLACK);
    }
}

/* Draw a tappable button with a filled black background. */
static void draw_active_button(fb_t *fb, int x, int y, int w, int h, const char *label) {
    fb_fill_rect(fb, x, y, w, h, COLOR_BLACK);
    int label_w = font_measure_string(label);
    font_draw_string(fb, x + (w - label_w) / 2, y + (h - FONT_CHAR_H) / 2,
                     label, COLOR_WHITE, COLOR_BLACK);
}

/* Draw a visually disabled button with a gray background. */
static void draw_inactive_button(fb_t *fb, int x, int y, int w, int h, const char *label) {
    fb_fill_rect(fb, x, y, w, h, COLOR_GRAY);
    int label_w = font_measure_string(label);
    font_draw_string(fb, x + (w - label_w) / 2, y + (h - FONT_CHAR_H) / 2,
                     label, COLOR_WHITE, COLOR_GRAY);
}

/* ── Public view functions ────────────────────────────────────────── */

void login_view_prefill(const char *handle, const char *password, const char *pds_url) {
    if (handle)   str_safe_copy(s_handle,   handle,   sizeof(s_handle));
    if (password) str_safe_copy(s_password, password, sizeof(s_password));
    if (pds_url)  str_safe_copy(s_pds_url,  pds_url,  sizeof(s_pds_url));
}

void login_view_draw(app_state_t *state) {
    fb_t *fb = &state->fb;
    fb_clear(fb, COLOR_WHITE);

    const char *title = "sKeets — Bluesky for Kobo";
    int title_w = font_measure_string(title);
    font_draw_string(fb, (FB_WIDTH - title_w) / 2, LOGIN_TITLE_Y,
                     title, COLOR_BLACK, COLOR_WHITE);

    const char *subtitle = "Sign in to your account";
    int subtitle_w = font_measure_string(subtitle);
    font_draw_string(fb, (FB_WIDTH - subtitle_w) / 2, LOGIN_SUBTITLE_Y,
                     subtitle, COLOR_GRAY, COLOR_WHITE);

    draw_text_field(fb, FIELD_X, HANDLE_Y, FIELD_W, FIELD_H,
                    "Handle or email", s_handle, s_focus == FOCUS_HANDLE);
    draw_password_field(fb, FIELD_X, PASS_Y, FIELD_W, FIELD_H,
                        "App password", s_password, s_focus == FOCUS_PASSWORD);
    draw_text_field(fb, FIELD_X, PDS_Y, FIELD_W, FIELD_H,
                    "PDS URL (blank = bsky.social)",
                    s_pds_url[0] ? s_pds_url : "", s_focus == FOCUS_PDS);

    int bx = login_btn_x();
    bool credentials_entered = s_handle[0] != '\0' && s_password[0] != '\0';
    if (credentials_entered)
        draw_active_button(fb, bx, BTN_Y, BTN_W, BTN_H, "Sign In");
    else
        draw_inactive_button(fb, bx, BTN_Y, BTN_W, BTN_H, "Sign In");

    if (s_error[0]) {
        int error_w = font_measure_string(s_error);
        font_draw_string(fb, (FB_WIDTH - error_w) / 2, BTN_Y + BTN_H + 24,
                         s_error, COLOR_BLACK, COLOR_WHITE);
    }

    const char *hint = "Tap a field, then use the on-screen keyboard";
    int hint_w = font_measure_string(hint);
    font_draw_string(fb, (FB_WIDTH - hint_w) / 2, BTN_Y + BTN_H + 60,
                     hint, COLOR_GRAY, COLOR_WHITE);

    fb_refresh_full(fb);
}

void login_view_handle(app_state_t *state, const input_event_t *ev) {
    if (ev->type != INPUT_TOUCH || ev->touch.type != TOUCH_UP) return;

    int tx = ev->touch.x;
    int ty = ev->touch.y;

    if (hit_test_rect(tx, ty, FIELD_X, HANDLE_Y, FIELD_W, FIELD_H)) {
        s_focus = FOCUS_HANDLE;
        login_view_draw(state);
        return;
    }

    if (hit_test_rect(tx, ty, FIELD_X, PASS_Y, FIELD_W, FIELD_H)) {
        s_focus = FOCUS_PASSWORD;
        login_view_draw(state);
        return;
    }

    if (hit_test_rect(tx, ty, FIELD_X, PDS_Y, FIELD_W, FIELD_H)) {
        s_focus = FOCUS_PDS;
        login_view_draw(state);
        return;
    }

    int bx = login_btn_x();
    bool credentials_entered = s_handle[0] && s_password[0];
    if (credentials_entered && hit_test_rect(tx, ty, bx, BTN_Y, BTN_W, BTN_H)) {
        s_error[0] = '\0';
        font_draw_string(&state->fb, bx, BTN_Y + BTN_H + 24,
                         "Signing in...              ", COLOR_BLACK, COLOR_WHITE);
        fb_refresh_partial(&state->fb, bx, BTN_Y + BTN_H + 24,
                           FB_WIDTH - bx * 2, FONT_CHAR_H);

        bool sign_in_succeeded = false;
        std::string err_msg;

        /* If the user specified a custom PDS URL, re-point the client
           before authenticating so non-Bluesky PDS users can log in. */
        if (s_pds_url[0]) {
            state->atproto_client->changeHost(s_pds_url);
        }

        state->atproto_client->createSession(s_handle, s_password,
            [state, &sign_in_succeeded](const Bsky::Session& sess) {
                state->session = sess;
                sign_in_succeeded = true;
            },
            [&err_msg](const std::string& e) {
                err_msg = e;
            });

        if (sign_in_succeeded) {
            config_t *cfg = config_open(CONFIG_PATH);
            if (cfg) {
                config_set_str(cfg, "handle",      state->session.handle.c_str());
                config_set_str(cfg, "access_jwt",  state->session.access_jwt.c_str());
                config_set_str(cfg, "refresh_jwt", state->session.refresh_jwt.c_str());
                config_set_str(cfg, "did",         state->session.did.c_str());
                config_set_str(cfg, "pds_url",     state->session.pds_url.c_str());
                config_save(cfg);
                config_free(cfg);
            }
            memset(s_password, 0, sizeof(s_password));
            app_switch_view(state, VIEW_FEED);
        } else {
            str_safe_copy(s_error, err_msg.empty() ? "Sign-in failed" : err_msg.c_str(),
                          sizeof(s_error));
            login_view_draw(state);
        }
    }
}
