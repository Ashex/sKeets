#pragma once

#include "ui/fb.h"
#include "ui/input.h"
#include "ui/views.h"
#include "atproto/atproto.h"
#include "atproto/atproto_client.h"

#include <memory>
#include <string>

struct app_state {
    app_view_t      current_view  = VIEW_AUTH_WAIT;
    Bsky::Session   session;
    Bsky::Feed      feed;
    std::shared_ptr<Bsky::Post> selected_post;
    std::string     pending_handle;
    std::string     pending_password;
    std::string     pending_pds_url;
    std::string     pending_appview_url;
    bool            profile_images_enabled = true;
    bool            embed_images_enabled   = false;
    bool            pending_login  = false;
    bool            running        = false;
    fb_t            fb{};
    input_ctx_t    *input          = nullptr;
    int             feed_scroll    = 0;
    int             thread_scroll  = 0;
    std::string     status_msg;
    bool            status_is_error = false;
    Bsky::AtprotoClient* atproto_client = nullptr;
    time_t          session_last_refresh = 0; /* time_t of last successful session refresh */
};

typedef struct app_state app_state_t;

int  app_init(app_state_t *state);
void app_shutdown(app_state_t *state);
void app_switch_view(app_state_t *state, app_view_t view);
/* Show a neutral informational message in the status bar. */
void app_set_info(app_state_t *state, const char *msg);
/* Show an error message in the status bar. */
void app_set_error(app_state_t *state, const char *msg);
void app_run(app_state_t *state);
int  app_ensure_auth(app_state_t *state);
