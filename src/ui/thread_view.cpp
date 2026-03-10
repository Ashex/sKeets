#include "thread_view.h"
#include "fb.h"
#include "font.h"
#include "../util/str.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory>

#define THREAD_INDENT      32
#define AVATAR_SZ          36
#define THREAD_PAD         10
#define CONTENT_X_0        (UI_MARGIN + AVATAR_SZ + 8)
#define MIN_CONTENT_WIDTH  80

static std::shared_ptr<Bsky::Post> s_thread_root;

static int content_width_for_indent(int indent) {
    int cw = FB_WIDTH - CONTENT_X_0 - indent - UI_MARGIN;
    return cw < MIN_CONTENT_WIDTH ? MIN_CONTENT_WIDTH : cw;
}

static int measure_thread_post_height(const Bsky::Post* post, int indent) {
    int cw = content_width_for_indent(indent);
    int h = THREAD_PAD;
    h += FONT_CHAR_H + 4;
    h += font_measure_wrapped(cw, post->text.c_str(), 2) + 4;
    h += FONT_CHAR_H + THREAD_PAD;
    h += 1;
    return h;
}

static void draw_thread_post(fb_t *fb, int y, const Bsky::Post* post,
                              int indent, int scroll) {
    int cw = content_width_for_indent(indent);
    int cx = CONTENT_X_0 + indent;
    int sy = y - scroll;
    int h  = measure_thread_post_height(post, indent);

    if (indent > 0)
        fb_vline(fb, UI_MARGIN + indent - 8, sy, h, COLOR_LGRAY);

    fb_fill_rect(fb, UI_MARGIN + indent, sy + THREAD_PAD, AVATAR_SZ, AVATAR_SZ, COLOR_LGRAY);

    int iy = sy + THREAD_PAD;

    char author[256];
    if (!post->author.display_name.empty())
        snprintf(author, sizeof(author), "%s  @%s",
                 post->author.display_name.c_str(), post->author.handle.c_str());
    else
        snprintf(author, sizeof(author), "@%s", post->author.handle.c_str());
    font_draw_string(fb, cx, iy, author, COLOR_BLACK, COLOR_WHITE);
    iy += FONT_CHAR_H + 4;

    int text_h = font_measure_wrapped(cw, post->text.c_str(), 2);
    font_draw_wrapped(fb, cx, iy, cw, post->text.c_str(), COLOR_BLACK, COLOR_WHITE, 2);
    iy += text_h + 4;

    char stats[128];
    snprintf(stats, sizeof(stats), "\xe2\x99\xa1 %d  \xe2\x86\xa9 %d  \xe2\x9f\xb3 %d  [Reply]",
             post->like_count, post->reply_count, post->repost_count);
    font_draw_string(fb, cx, iy, stats, COLOR_GRAY, COLOR_WHITE);

    /* Draw a vertical connector line below the root post to indicate threading. */
    const bool is_root_post = (indent == 0);
    const bool has_replies  = !post->replies.empty();
    if (is_root_post && has_replies) {
        fb_vline(fb, UI_MARGIN + AVATAR_SZ / 2, sy + THREAD_PAD + AVATAR_SZ,
                 h - THREAD_PAD - AVATAR_SZ, COLOR_LGRAY);
    }

    fb_hline(fb, indent, sy + h - 1, FB_WIDTH - indent, COLOR_LGRAY);
}

struct draw_node_t {
    const Bsky::Post* post;
    int indent;
};

#define MAX_DRAW_NODES 128

static draw_node_t s_nodes[MAX_DRAW_NODES];
static int         s_node_y[MAX_DRAW_NODES];
static int         s_node_h[MAX_DRAW_NODES];
static int         s_node_count = 0;
static bool        s_nodes_dirty = true;

static void flatten_thread(const Bsky::Post* post, int indent) {
    if (!post || s_node_count >= MAX_DRAW_NODES) return;
    s_nodes[s_node_count].post   = post;
    s_nodes[s_node_count].indent = indent;
    s_node_count++;
    for (const auto& reply : post->replies)
        flatten_thread(reply.get(), indent + THREAD_INDENT);
}

static void compute_thread_layout(int start_y) {
    int y = start_y;
    for (int i = 0; i < s_node_count; i++) {
        s_node_y[i] = y;
        s_node_h[i] = measure_thread_post_height(s_nodes[i].post, s_nodes[i].indent);
        y += s_node_h[i];
    }
    s_nodes_dirty = false;
}

static void draw_topbar(fb_t *fb) {
    fb_fill_rect(fb, 0, 0, FB_WIDTH, UI_BAR_H, COLOR_WHITE);
    fb_hline(fb, 0, UI_BAR_H - 1, FB_WIDTH, COLOR_BLACK);
    font_draw_string(fb, UI_MARGIN, (UI_BAR_H - FONT_CHAR_H) / 2,
                     "< Back", COLOR_BLACK, COLOR_WHITE);
    const char *title = "Thread";
    int tw = font_measure_string(title);
    font_draw_string(fb, (FB_WIDTH - tw) / 2, (UI_BAR_H - FONT_CHAR_H) / 2,
                     title, COLOR_BLACK, COLOR_WHITE);
    font_draw_string(fb, FB_WIDTH - 100, (UI_BAR_H - FONT_CHAR_H) / 2,
                     "Reply", COLOR_BLACK, COLOR_WHITE);
}

void thread_view_load(app_state_t *state) {
    if (!state->selected_post) return;

    s_thread_root.reset();
    s_node_count = 0;

    state->atproto_client->getPostThread(state->selected_post->uri,
        [state](const Bsky::Post& root) {
            s_thread_root = std::make_shared<Bsky::Post>(root);
            s_node_count = 0;
            flatten_thread(s_thread_root.get(), 0);
            compute_thread_layout(UI_BAR_H);
        },
        [state](const std::string&) {
            app_set_error(state, "Failed to load thread");
        });
    s_nodes_dirty = false;
}

void thread_view_draw(app_state_t *state) {
    fb_t *fb = &state->fb;
    fb_clear(fb, COLOR_WHITE);
    draw_topbar(fb);

    if (s_node_count == 0) {
        if (state->selected_post) {
            font_draw_wrapped(fb, UI_MARGIN, UI_BAR_H + 16,
                              FB_WIDTH - UI_MARGIN * 2,
                              state->selected_post->text.c_str(),
                              COLOR_BLACK, COLOR_WHITE, 2);
        }
        font_draw_string(fb, UI_MARGIN, FB_HEIGHT / 2, "Loading thread...",
                         COLOR_GRAY, COLOR_WHITE);
        fb_refresh_full(fb);
        return;
    }

    int content_top = UI_BAR_H;
    int content_bot = FB_HEIGHT - UI_BAR_H;
    int virtual_h   = s_node_y[s_node_count - 1] + s_node_h[s_node_count - 1];
    int max_scroll  = virtual_h - (content_bot - content_top);
    state->thread_scroll = clamp_scroll(state->thread_scroll, max_scroll);

    for (int i = 0; i < s_node_count; i++) {
        int sy = s_node_y[i] - state->thread_scroll;
        if (sy + s_node_h[i] < content_top) continue;
        if (sy > content_bot) break;
        draw_thread_post(fb, s_node_y[i], s_nodes[i].post, s_nodes[i].indent,
                         state->thread_scroll);
    }

    fb_fill_rect(fb, 0, FB_HEIGHT - UI_BAR_H, FB_WIDTH, UI_BAR_H, COLOR_WHITE);
    fb_hline(fb, 0, FB_HEIGHT - UI_BAR_H, FB_WIDTH, COLOR_BLACK);
    if (!state->status_msg.empty()) {
        font_draw_string(fb, UI_MARGIN,
                         FB_HEIGHT - UI_BAR_H + (UI_BAR_H - FONT_CHAR_H) / 2,
                         state->status_msg.c_str(), COLOR_GRAY, COLOR_WHITE);
    }

    fb_refresh_full(fb);
}

void thread_view_handle(app_state_t *state, const input_event_t *ev) {
    static touch_event_t last_down{};
    static int last_scroll = 0;

    if (ev->type == INPUT_TOUCH) {
        if (ev->touch.type == TOUCH_DOWN) {
            last_down   = ev->touch;
            last_scroll = state->thread_scroll;
        } else if (ev->touch.type == TOUCH_MOVE) {
            int dy = last_down.y - ev->touch.y;
            state->thread_scroll = last_scroll + dy;
            thread_view_draw(state);
        } else if (ev->touch.type == TOUCH_UP) {
            if (!input_is_tap(&last_down, &ev->touch)) return;
            int tx = ev->touch.x;
            int ty = ev->touch.y;

            if (ty < UI_BAR_H && tx < 120) {
                s_thread_root.reset();
                s_node_count = 0;
                app_switch_view(state, VIEW_FEED);
                return;
            }

            if (ty < UI_BAR_H && tx > FB_WIDTH - 120) {
                if (state->selected_post) {
                    state->compose_reply_uri = state->selected_post->uri;
                    state->compose_reply_cid = state->selected_post->cid;
                    if (s_thread_root) {
                        state->compose_root_uri = s_thread_root->uri;
                        state->compose_root_cid = s_thread_root->cid;
                    } else {
                        state->compose_root_uri = state->selected_post->uri;
                        state->compose_root_cid = state->selected_post->cid;
                    }
                }
                app_switch_view(state, VIEW_COMPOSE);
                return;
            }

            int content_y = ty + state->thread_scroll;
            for (int i = 0; i < s_node_count; i++) {
                if (content_y >= s_node_y[i] &&
                    content_y < s_node_y[i] + s_node_h[i]) {
                    const Bsky::Post* tapped = s_nodes[i].post;
                    state->compose_reply_uri = tapped->uri;
                    state->compose_reply_cid = tapped->cid;
                    const Bsky::Post* root_p = s_thread_root ? s_thread_root.get() : tapped;
                    state->compose_root_uri = root_p->uri;
                    state->compose_root_cid = root_p->cid;
                    app_switch_view(state, VIEW_COMPOSE);
                    return;
                }
            }
        }
    }
}
