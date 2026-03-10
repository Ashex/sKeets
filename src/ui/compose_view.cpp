#include "compose_view.h"
#include "fb.h"
#include "font.h"
#include "../util/str.h"

#include <string.h>
#include <stdio.h>
#include <optional>

#define MAX_POST_LEN      300
#define TEXT_AREA_X       UI_MARGIN
#define TEXT_AREA_Y       (UI_BAR_H + 16)
#define TEXT_AREA_W       (FB_WIDTH - UI_MARGIN * 2)
#define TEXT_AREA_H       (FB_HEIGHT - UI_BAR_H * 2 - 32)
#define POST_BTN_W        80
#define POST_BTN_H        36
#define POST_BTN_X        (FB_WIDTH - POST_BTN_W - 10)
#define POST_BTN_LABEL_X  (FB_WIDTH - POST_BTN_W - 2)

static char s_compose_text[MAX_POST_LEN + 1];

static void draw_topbar(fb_t *fb, const app_state_t *state) {
    fb_fill_rect(fb, 0, 0, FB_WIDTH, UI_BAR_H, COLOR_WHITE);
    fb_hline(fb, 0, UI_BAR_H - 1, FB_WIDTH, COLOR_BLACK);

    font_draw_string(fb, UI_MARGIN, (UI_BAR_H - FONT_CHAR_H) / 2,
                     "Cancel", COLOR_BLACK, COLOR_WHITE);

    const char *title = !state->compose_reply_uri.empty() ? "Reply" : "New Post";
    int tw = font_measure_string(title);
    font_draw_string(fb, (FB_WIDTH - tw) / 2, (UI_BAR_H - FONT_CHAR_H) / 2,
                     title, COLOR_BLACK, COLOR_WHITE);

    int len = (int)strlen(s_compose_text);
    bool can_post = len > 0 && len <= MAX_POST_LEN;
    uint16_t post_btn_bg = can_post ? COLOR_BLACK : COLOR_GRAY;
    fb_fill_rect(fb, POST_BTN_X, (UI_BAR_H - POST_BTN_H) / 2, POST_BTN_W, POST_BTN_H, post_btn_bg);
    font_draw_string(fb, POST_BTN_LABEL_X, (UI_BAR_H - FONT_CHAR_H) / 2,
                     "Post", COLOR_WHITE, post_btn_bg);
}

static void draw_context(fb_t *fb, const app_state_t *state) {
    if (state->compose_reply_uri.empty()) return;
    font_draw_string(fb, UI_MARGIN, UI_BAR_H + 4, "Replying to a post", COLOR_GRAY, COLOR_WHITE);
}

static void draw_text_area(fb_t *fb) {
    fb_fill_rect(fb, TEXT_AREA_X, TEXT_AREA_Y, TEXT_AREA_W, TEXT_AREA_H, COLOR_WHITE);
    fb_draw_rect(fb, TEXT_AREA_X, TEXT_AREA_Y, TEXT_AREA_W, TEXT_AREA_H, COLOR_LGRAY, 1);

    font_draw_wrapped(fb, TEXT_AREA_X + 8, TEXT_AREA_Y + 8,
                      TEXT_AREA_W - 16, s_compose_text,
                      COLOR_BLACK, COLOR_WHITE, 2);

    char count_buf[16];
    snprintf(count_buf, sizeof(count_buf), "%d/%d", (int)strlen(s_compose_text), MAX_POST_LEN);
    int cw = font_measure_string(count_buf);
    font_draw_string(fb, TEXT_AREA_X + TEXT_AREA_W - cw - 8,
                     TEXT_AREA_Y + TEXT_AREA_H - FONT_CHAR_H - 8,
                     count_buf,
                     (int)strlen(s_compose_text) > MAX_POST_LEN ? COLOR_BLACK : COLOR_GRAY,
                     COLOR_WHITE);

    if (!s_compose_text[0]) {
        font_draw_string(fb, TEXT_AREA_X + 8, TEXT_AREA_Y + 8,
                         "What's on your mind?", COLOR_LGRAY, COLOR_WHITE);
    }
}

static void draw_keyboard_hint(fb_t *fb) {
    int y = FB_HEIGHT - UI_BAR_H;
    fb_fill_rect(fb, 0, y, FB_WIDTH, UI_BAR_H, COLOR_WHITE);
    fb_hline(fb, 0, y, FB_WIDTH, COLOR_BLACK);
    font_draw_string(fb, UI_MARGIN, y + (UI_BAR_H - FONT_CHAR_H) / 2,
                     "Tap text area to type (Kobo virtual keyboard)", COLOR_GRAY, COLOR_WHITE);
}

void compose_view_draw(app_state_t *state) {
    fb_t *fb = &state->fb;
    fb_clear(fb, COLOR_WHITE);
    draw_topbar(fb, state);
    draw_context(fb, state);
    draw_text_area(fb);
    draw_keyboard_hint(fb);
    fb_refresh_full(fb);
}

void compose_view_handle(app_state_t *state, const input_event_t *ev) {
    if (ev->type != INPUT_TOUCH || ev->touch.type != TOUCH_UP) return;
    int tx = ev->touch.x;
    int ty = ev->touch.y;

    if (ty < UI_BAR_H && tx < 120) {
        memset(s_compose_text, 0, sizeof(s_compose_text));
        if (!state->compose_reply_uri.empty())
            app_switch_view(state, VIEW_THREAD);
        else
            app_switch_view(state, VIEW_FEED);
        return;
    }

    if (ty < UI_BAR_H && tx > POST_BTN_X) {
        int len = (int)strlen(s_compose_text);
        if (len > 0 && len <= MAX_POST_LEN) {
            int rc = compose_view_submit(state, s_compose_text);
            if (rc == 0) {
                memset(s_compose_text, 0, sizeof(s_compose_text));
                app_set_info(state, "Posted!");
                if (!state->compose_reply_uri.empty())
                    app_switch_view(state, VIEW_THREAD);
                else {
                    app_switch_view(state, VIEW_FEED);
                    extern void feed_view_refresh(app_state_t *);
                    feed_view_refresh(state);
                }
            } else {
                app_set_error(state, "Failed to post");
                compose_view_draw(state);
            }
        }
        return;
    }
}

int compose_view_submit(app_state_t *state, const char *text) {
    if (!text || !text[0]) return -1;
    if (app_ensure_auth(state) != 0) return -1;

    bool ok = false;

    std::optional<std::string> parent_uri, parent_cid, root_uri, root_cid;
    if (!state->compose_reply_uri.empty()) {
        parent_uri = state->compose_reply_uri;
        parent_cid = state->compose_reply_cid;
        root_uri   = state->compose_root_uri.empty() ? state->compose_reply_uri : state->compose_root_uri;
        root_cid   = state->compose_root_cid.empty() ? state->compose_reply_cid : state->compose_root_cid;
    }

    state->atproto_client->createPost(text,
        parent_uri, parent_cid, root_uri, root_cid,
        [&ok](const std::string&, const std::string&) { ok = true; },
        [](const std::string&) {});

    return ok ? 0 : -1;
}
