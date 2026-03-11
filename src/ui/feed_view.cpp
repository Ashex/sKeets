#include "feed_view.h"
#include "fb.h"
#include "font.h"
#include "../util/str.h"
#include "../util/image.h"
#include "../util/image_cache.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define POST_MARGIN      UI_MARGIN
#define POST_PAD         10
#define AVATAR_SIZE      40
#define POST_DIVIDER_H   1
#define CONTENT_X        (POST_MARGIN + AVATAR_SIZE + POST_PAD)
#define QUOTE_INDENT     24
#define QUOTE_BORDER     3

#define MAX_CACHED_POSTS 200
static int s_post_heights[MAX_CACHED_POSTS];
static int s_post_y[MAX_CACHED_POSTS];
static int s_virtual_height;
static bool s_layout_dirty = true;

static int measure_post_height(const Bsky::Post& post, bool embed_images,
                                int content_w, int font_h) {
    int h = POST_PAD;
    if (!post.reposted_by.empty())
        h += font_h + 2;
    h += font_h + 4;
    h += font_measure_wrapped(content_w, post.text.c_str(), 2) + 4;
    if (post.embed_type == Bsky::EmbedType::Image && embed_images && !post.image_urls.empty())
        h += 120 + 4;
    else if (post.embed_type == Bsky::EmbedType::Quote || post.embed_type == Bsky::EmbedType::RecordWithMedia) {
        if (post.quoted_post) {
            h += POST_PAD;
            h += font_h + 2;
            h += font_measure_wrapped(content_w - QUOTE_INDENT - 8,
                                      post.quoted_post->text.c_str(), 2) + 8;
        }
    } else if (post.embed_type == Bsky::EmbedType::External) {
        h += font_h * 2 + 12;
    }
    h += font_h + POST_PAD;
    h += POST_DIVIDER_H;
    return h;
}

static void compute_layout(const Bsky::Feed& feed, bool embed_images, int fb_w, int font_h) {
    int content_w = fb_w - CONTENT_X - POST_MARGIN;
    int y = UI_BAR_H;
    int count = (int)feed.items.size();
    for (int i = 0; i < count && i < MAX_CACHED_POSTS; i++) {
        s_post_y[i] = y;
        s_post_heights[i] = measure_post_height(feed.items[i], embed_images, content_w, font_h);
        y += s_post_heights[i];
    }
    s_virtual_height = y + UI_BAR_H;
    s_layout_dirty = false;
}

static void draw_stats_line(fb_t *fb, int x, int y, const Bsky::Post& post) {
    char buf[256];
    const char *heart       = post.viewer_like.empty()   ? "\xe2\x99\xa1" : "\xe2\x99\xa5";
    const char *repost_icon = post.viewer_repost.empty() ? "\xe2\x9f\xb3" : "\xe2\x9f\xb2";
    snprintf(buf, sizeof(buf), "%s %d  \xe2\x86\xa9 %d  %s %d",
             heart, post.like_count,
             post.reply_count,
             repost_icon, post.repost_count);
    font_draw_string(fb, x, y, buf, COLOR_GRAY, COLOR_WHITE);
}

static void draw_repost_line(fb_t *fb, int x, int y, const Bsky::Post& post) {
    if (post.reposted_by.empty()) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "\xe2\x9f\xb3 Reposted by @%s", post.reposted_by.c_str());
    font_draw_string(fb, x, y, buf, COLOR_GRAY, COLOR_WHITE);
}

static void draw_author_line(fb_t *fb, int x, int y, const Bsky::Post& post) {
    char line[384];
    char time_buf[32];
    str_format_time(time_buf, sizeof(time_buf), (long)post.indexed_at);
    if (!post.author.display_name.empty())
        snprintf(line, sizeof(line), "%s  @%s  %s",
                 post.author.display_name.c_str(), post.author.handle.c_str(), time_buf);
    else
        snprintf(line, sizeof(line), "@%s  %s", post.author.handle.c_str(), time_buf);
    font_draw_string(fb, x, y, line, COLOR_BLACK, COLOR_WHITE);
}

static void draw_quote_embed(fb_t *fb, int x, int y, int w, const Bsky::Post* quoted) {
    if (!quoted) return;
    int font_h = font_cell_h();
    int qh = font_h + 4 +
             font_measure_wrapped(w - QUOTE_INDENT - 8, quoted->text.c_str(), 2) + 8;
    fb_fill_rect(fb, x, y, QUOTE_BORDER, qh, COLOR_GRAY);
    fb_draw_rect(fb, x, y, w, qh, COLOR_LGRAY, 1);

    int qx = x + QUOTE_INDENT;
    int qw = w - QUOTE_INDENT - 8;
    int qy = y + POST_PAD / 2;

    char author_line[256];
    if (!quoted->author.display_name.empty())
        snprintf(author_line, sizeof(author_line), "%s  @%s",
                 quoted->author.display_name.c_str(), quoted->author.handle.c_str());
    else
        snprintf(author_line, sizeof(author_line), "@%s", quoted->author.handle.c_str());
    font_draw_string(fb, qx, qy, author_line, COLOR_DGRAY, COLOR_WHITE);
    qy += font_h + 2;
    font_draw_wrapped(fb, qx, qy, qw, quoted->text.c_str(), COLOR_BLACK, COLOR_WHITE, 2);
}

static void draw_external_embed(fb_t *fb, int x, int y, int w, const Bsky::Post& post) {
    int font_h = font_cell_h();
    fb_draw_rect(fb, x, y, w, font_h * 2 + 12, COLOR_LGRAY, 1);
    font_draw_string(fb, x + 6, y + 4, post.ext_title.c_str(), COLOR_BLACK, COLOR_WHITE);
    font_draw_string(fb, x + 6, y + 4 + font_h + 4, post.ext_uri.c_str(), COLOR_GRAY, COLOR_WHITE);
}

static void draw_post(fb_t *fb, int draw_y, const Bsky::Post& post,
                      bool profile_images, bool embed_images,
                      int scroll, int post_height, int fb_w) {
    int content_w = fb_w - CONTENT_X - POST_MARGIN;
    int font_h = font_cell_h();
    int screen_y = draw_y - scroll;
    int y = screen_y + POST_PAD;

    if (!post.reposted_by.empty()) {
        draw_repost_line(fb, CONTENT_X, y, post);
        y += font_h + 2;
    }

    int av_x = POST_MARGIN;
    if (!post.author.avatar_url.empty() && profile_images) {
        const image_t *av = image_cache_lookup(post.author.avatar_url.c_str(),
                                               AVATAR_SIZE, AVATAR_SIZE);
        if (av)
            fb_blit_rgba(fb, av_x, y, av->width, av->height, av->pixels);
        else
            fb_fill_rect(fb, av_x, y, AVATAR_SIZE, AVATAR_SIZE, COLOR_LGRAY);
    } else {
        fb_fill_rect(fb, av_x, y, AVATAR_SIZE, AVATAR_SIZE, COLOR_LGRAY);
    }

    draw_author_line(fb, CONTENT_X, y, post);
    y += font_h + 4;

    int text_h = font_measure_wrapped(content_w, post.text.c_str(), 2);
    font_draw_wrapped(fb, CONTENT_X, y, content_w, post.text.c_str(), COLOR_BLACK, COLOR_WHITE, 2);
    y += text_h + 4;

    switch (post.embed_type) {
        case Bsky::EmbedType::Image:
            if (embed_images && !post.image_urls.empty()) {
                const image_t *embed = image_cache_lookup(
                        post.image_urls[0].c_str(), content_w, 112);
                if (embed) {
                    fb_blit_rgba(fb, CONTENT_X, y, embed->width, embed->height,
                                 embed->pixels);
                    y += embed->height + 8;
                } else {
                    fb_fill_rect(fb, CONTENT_X, y, content_w, 112, COLOR_LGRAY);
                    font_draw_string(fb, CONTENT_X + 4, y + 48, "[image]",
                                     COLOR_GRAY, COLOR_LGRAY);
                    y += 120;
                }
            }
            break;
        case Bsky::EmbedType::Quote:
        case Bsky::EmbedType::RecordWithMedia:
            if (post.quoted_post) {
                int qw = content_w - QUOTE_INDENT;
                int qh = font_h + 4 +
                         font_measure_wrapped(qw - 8, post.quoted_post->text.c_str(), 2) + 8;
                draw_quote_embed(fb, CONTENT_X, y, qw, post.quoted_post.get());
                y += qh + POST_PAD;
            }
            break;
        case Bsky::EmbedType::External:
            draw_external_embed(fb, CONTENT_X, y, content_w, post);
            y += font_cell_h() * 2 + 16;
            break;
        default:
            break;
    }

    draw_stats_line(fb, CONTENT_X, y, post);

    fb_hline(fb, 0, screen_y + post_height - POST_DIVIDER_H, fb_w, COLOR_LGRAY);
}

static void draw_topbar(fb_t *fb, int fb_w) {
    int font_h = font_cell_h();
    fb_fill_rect(fb, 0, 0, fb_w, UI_BAR_H, COLOR_WHITE);
    fb_hline(fb, 0, UI_BAR_H - 1, fb_w, COLOR_BLACK);

    const char *title = "Home";
    int tw = font_measure_string(title);
    font_draw_string(fb, (fb_w - tw) / 2, (UI_BAR_H - font_h) / 2,
                     title, COLOR_BLACK, COLOR_WHITE);
    font_draw_string(fb, fb_w - 80, (UI_BAR_H - font_h) / 2,
                     "Settings", COLOR_BLACK, COLOR_WHITE);
    font_draw_string(fb, UI_MARGIN, (UI_BAR_H - font_h) / 2,
                     "Refresh", COLOR_BLACK, COLOR_WHITE);
}

static void draw_bottombar(fb_t *fb, const app_state_t *state, int fb_w, int fb_h) {
    int font_h = font_cell_h();
    int y = fb_h - UI_BAR_H;
    fb_fill_rect(fb, 0, y, fb_w, UI_BAR_H, COLOR_WHITE);
    fb_hline(fb, 0, y, fb_w, COLOR_BLACK);
    if (!state->status_msg.empty()) {
        uint8_t col = state->status_is_error ? COLOR_BLACK : COLOR_GRAY;
        font_draw_string(fb, UI_MARGIN, y + (UI_BAR_H - font_h) / 2,
                         state->status_msg.c_str(), col, COLOR_WHITE);
    }
}

void feed_view_draw(app_state_t *state) {
    fb_t *fb = &state->fb;
    int fb_w = fb->width;
    int fb_h = fb->height;
    int font_h = font_cell_h();
    fb_clear(fb, COLOR_WHITE);

    if (s_layout_dirty)
        compute_layout(state->feed, state->embed_images_enabled, fb_w, font_h);

    draw_topbar(fb, fb_w);
    draw_bottombar(fb, state, fb_w, fb_h);

    int content_top = UI_BAR_H;
    int content_bot = fb_h - UI_BAR_H;
    int content_h   = content_bot - content_top;

    int max_scroll = s_virtual_height - content_h;
    state->feed_scroll = clamp_scroll(state->feed_scroll, max_scroll);

    int count = (int)state->feed.items.size();
    for (int i = 0; i < count && i < MAX_CACHED_POSTS; i++) {
        int post_screen_y = s_post_y[i] - state->feed_scroll;
        if (post_screen_y + s_post_heights[i] < content_top) continue;
        if (post_screen_y > content_bot) break;
        draw_post(fb, s_post_y[i], state->feed.items[i],
                  state->profile_images_enabled, state->embed_images_enabled,
                  state->feed_scroll, s_post_heights[i], fb_w);
    }

    if (state->feed.items.empty()) {
        const char *empty = "Pull to refresh or tap Refresh to load your timeline";
        int ew = font_measure_string(empty);
        font_draw_string(fb, (fb_w - ew) / 2,
                         content_top + content_h / 2 - font_h / 2,
                         empty, COLOR_GRAY, COLOR_WHITE);
    }

    fb_refresh_full(fb);
}

void feed_view_handle(app_state_t *state, const input_event_t *ev) {
    static touch_event_t last_down{};
    static int last_scroll = 0;

    if (ev->type == INPUT_TOUCH) {
        if (ev->touch.type == TOUCH_DOWN) {
            last_down   = ev->touch;
            last_scroll = state->feed_scroll;
        } else if (ev->touch.type == TOUCH_MOVE) {
            int dy = last_down.y - ev->touch.y;
            state->feed_scroll = last_scroll + dy;
            feed_view_draw(state);
        } else if (ev->touch.type == TOUCH_UP) {
            int tx = ev->touch.x;
            int ty = ev->touch.y;
            bool is_tap = input_is_tap(&last_down, &ev->touch);

            if (is_tap) {
                if (ty < UI_BAR_H) {
                    if (tx < 120) {
                        feed_view_refresh(state);
                    } else if (tx > state->fb.width - 120) {
                        app_switch_view(state, VIEW_SETTINGS);
                    }
                    return;
                }

                int content_y = ty + state->feed_scroll;
                int count = (int)state->feed.items.size();
                for (int i = 0; i < count && i < MAX_CACHED_POSTS; i++) {
                    if (content_y >= s_post_y[i] &&
                        content_y < s_post_y[i] + s_post_heights[i]) {
                        // Check if tap is in stats zone (near bottom of post)
                        int stats_y_in_post = s_post_heights[i] - font_cell_h() - POST_PAD;
                        int stats_content_y = content_y - s_post_y[i];
                        if (stats_content_y >= stats_y_in_post - 8 &&
                            stats_content_y <  stats_y_in_post + font_cell_h() + 8) {
                            int screen_tx = ev->touch.x;
                            int like_x_approx   = CONTENT_X;
                            int repost_x_approx = CONTENT_X + 120;

                            if (screen_tx < like_x_approx + 60) {
                                // Like / unlike
                                Bsky::Post& p = state->feed.items[i];
                                if (p.viewer_like.empty()) {
                                    p.like_count++;
                                    feed_view_draw(state);
                                    state->atproto_client->likePost(p.uri, p.cid,
                                        [state, i](const std::string& like_uri) {
                                            if (i < (int)state->feed.items.size())
                                                state->feed.items[i].viewer_like = like_uri;
                                        },
                                        [state, i](const std::string&) {
                                            if (i < (int)state->feed.items.size()) {
                                                state->feed.items[i].like_count--;
                                                feed_view_draw(state);
                                            }
                                        });
                                } else {
                                    p.like_count--;
                                    std::string old_like_uri = p.viewer_like;
                                    p.viewer_like.clear();
                                    feed_view_draw(state);
                                    state->atproto_client->unlikePost(old_like_uri,
                                        [](){},
                                        [state, i, old_like_uri](const std::string&) {
                                            if (i < (int)state->feed.items.size()) {
                                                state->feed.items[i].like_count++;
                                                state->feed.items[i].viewer_like = old_like_uri;
                                                feed_view_draw(state);
                                            }
                                        });
                                }
                                return;
                            } else if (screen_tx < repost_x_approx + 60) {
                                // Repost / unrepost
                                Bsky::Post& p = state->feed.items[i];
                                if (p.viewer_repost.empty()) {
                                    p.repost_count++;
                                    feed_view_draw(state);
                                    state->atproto_client->repostPost(p.uri, p.cid,
                                        [state, i](const std::string& repost_uri) {
                                            if (i < (int)state->feed.items.size())
                                                state->feed.items[i].viewer_repost = repost_uri;
                                        },
                                        [state, i](const std::string&) {
                                            if (i < (int)state->feed.items.size()) {
                                                state->feed.items[i].repost_count--;
                                                feed_view_draw(state);
                                            }
                                        });
                                } else {
                                    p.repost_count--;
                                    std::string old_repost_uri = p.viewer_repost;
                                    p.viewer_repost.clear();
                                    feed_view_draw(state);
                                    state->atproto_client->unrepostPost(old_repost_uri,
                                        [](){},
                                        [state, i, old_repost_uri](const std::string&) {
                                            if (i < (int)state->feed.items.size()) {
                                                state->feed.items[i].repost_count++;
                                                state->feed.items[i].viewer_repost = old_repost_uri;
                                                feed_view_draw(state);
                                            }
                                        });
                                }
                                return;
                            }
                        }
                        // Normal tap → navigate to thread
                        state->selected_post = std::make_shared<Bsky::Post>(state->feed.items[i]);
                        app_switch_view(state, VIEW_THREAD);
                        return;
                    }
                }
            }
        }
    }
}

void feed_view_refresh(app_state_t *state) {
    if (app_ensure_auth(state) != 0) {
        app_set_error(state, "Auth error - please create login.txt and restart");
        app_switch_view(state, VIEW_AUTH_WAIT);
        return;
    }

    app_set_info(state, "Loading...");
    feed_view_draw(state);

    state->feed = Bsky::Feed{};
    s_layout_dirty = true;

    state->atproto_client->getTimeline(FEED_PAGE_SIZE, std::nullopt,
        [state](const Bsky::Feed& f) {
            state->feed = f;
            s_layout_dirty = true;
            char buf[64];
            snprintf(buf, sizeof(buf), "%d posts loaded", (int)state->feed.items.size());
            app_set_info(state, buf);
        },
        [state](const std::string&) {
            app_set_error(state, "Failed to load timeline");
        });

    feed_view_draw(state);
}
