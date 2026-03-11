#include "app.h"
#include "ui/login_view.h"
#include "ui/feed_view.h"
#include "ui/thread_view.h"
#include "ui/compose_view.h"
#include "ui/settings_view.h"
#include "ui/font.h"
#include "util/config.h"
#include "util/device.h"
#include "util/image.h"
#include "util/image_cache.h"
#include "util/paths.h"
#include "util/str.h"

#include <QCoreApplication>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int app_init(app_state_t *state) {
    if (!state) return -1;
    *state = app_state_t{};
    state->running = true;
    state->current_view = VIEW_LOGIN;

    const std::string kobo_version = device_kobo_version_string();
    const std::string kobo_model = device_kobo_model_string();
    fprintf(stderr, "app_init: device model=%s version=%s\n",
            kobo_model.c_str(),
            kobo_version.empty() ? "unknown" : kobo_version.c_str());

    if (skeets_ensure_data_dirs() != 0) {
        fprintf(stderr, "app_init: warning: failed to prepare data directories under %s\n",
                skeets_data_dir());
    }

    if (fb_open(&state->fb) != 0) {
        fprintf(stderr, "app_init: failed to open framebuffer\n");
        return -1;
    }
    fprintf(stderr, "app_init: framebuffer ready width=%d height=%d\n",
            state->fb.width, state->fb.height);

    /* OpenType rendering is disabled for now because FBInk bitmap text is the
       only path that renders reliably on-device across current builds. */
    font_init(&state->fb);
    font_set_ot_enabled(false);
    fprintf(stderr, "app_init: font subsystem initialised (FBInk fallback text)\n");

    state->input = input_open();
    if (!state->input)
        fprintf(stderr, "app_init: warning: failed to open input devices\n");
    else
        fprintf(stderr, "app_init: input devices opened\n");

    config_t *cfg = config_open(skeets_config_path());
    std::string saved_pds_url;
    if (cfg) {
        state->images_enabled = config_get_bool(cfg, "images_enabled", false);
        saved_pds_url         = config_get_str(cfg, "pds_url", "");
    }

    /* Create the client pointed at the saved PDS (or default bsky.social). */
    state->atproto_client = new Bsky::AtprotoClient(saved_pds_url);
        fprintf(stderr, "app_init: atproto client initialised host=%s\n",
            saved_pds_url.empty() ? "https://bsky.social" : saved_pds_url.c_str());

    if (cfg) {
        const char *saved_handle   = config_get_str(cfg, "handle",       "");
        const char *saved_access   = config_get_str(cfg, "access_jwt",   "");
        const char *saved_refresh  = config_get_str(cfg, "refresh_jwt",  "");
        const char *saved_did      = config_get_str(cfg, "did",          "");
        const char *prefill_pass   = config_get_str(cfg, "app_password", "");

        const bool has_saved_session = saved_handle[0] && saved_access[0] && saved_did[0];
        if (has_saved_session) {
            state->session.handle      = saved_handle;
            state->session.access_jwt  = saved_access;
            state->session.refresh_jwt = saved_refresh;
            state->session.did         = saved_did;
            state->session.pds_url     = saved_pds_url;

            bool session_resumed = false;
            state->atproto_client->resumeSession(state->session,
                [&session_resumed]() { session_resumed = true; },
                [](const std::string&) {});
            if (session_resumed)
                state->current_view = VIEW_FEED;

        } else if (saved_handle[0] && prefill_pass[0]) {
            /* Pre-filled credentials from config.ini – attempt auto-login.
               If it works the password is scrubbed from the config file. */
            if (!saved_pds_url.empty())
                state->atproto_client->changeHost(saved_pds_url);

            bool ok = false;
            std::string err_msg;
            state->atproto_client->createSession(saved_handle, prefill_pass,
                [state, &ok](const Bsky::Session& sess) {
                    state->session = sess;
                    ok = true;
                },
                [&err_msg](const std::string& e) {
                    err_msg = e;
                });

            /* Always remove the plain-text password from the config. */
            config_set_str(cfg, "app_password", "");
            if (ok) {
                config_set_str(cfg, "handle",      state->session.handle.c_str());
                config_set_str(cfg, "access_jwt",  state->session.access_jwt.c_str());
                config_set_str(cfg, "refresh_jwt", state->session.refresh_jwt.c_str());
                config_set_str(cfg, "did",         state->session.did.c_str());
                config_set_str(cfg, "pds_url",     state->session.pds_url.c_str());
                config_save(cfg);
                state->current_view = VIEW_FEED;
            } else {
                config_save(cfg);
                /* Pre-populate login fields so user can correct & retry. */
                login_view_prefill(saved_handle, nullptr, saved_pds_url.c_str());
                app_set_error(state, err_msg.empty()
                    ? "Auto sign-in failed" : err_msg.c_str());
            }
        } else if (saved_handle[0]) {
            /* Handle is known but no session – pre-populate login field. */
            login_view_prefill(saved_handle, nullptr, saved_pds_url.c_str());
        }

        config_free(cfg);
    }

    return 0;
}

void app_shutdown(app_state_t *state) {
    if (!state) return;
    fprintf(stderr, "app_shutdown: begin\n");
    delete state->atproto_client;
    state->atproto_client = nullptr;
    if (state->input) input_close(state->input);
    fb_close(&state->fb);
    fprintf(stderr, "app_shutdown: complete\n");
}

void app_set_info(app_state_t *state, const char *msg) {
    if (!state) return;
    state->status_msg      = msg ? msg : "";
    state->status_is_error = false;
}

void app_set_error(app_state_t *state, const char *msg) {
    if (!state) return;
    state->status_msg      = msg ? msg : "";
    state->status_is_error = true;
}

int app_ensure_auth(app_state_t *state) {
    if (!state || !state->atproto_client) return -1;
    if (state->session.access_jwt.empty()) return -1;
    if (state->atproto_client->hasSession()) return 0;

    bool ok = false;
    state->atproto_client->resumeSession(state->session,
        [&ok, state]() {
            ok = true;
            config_t *cfg = config_open(skeets_config_path());
            if (cfg) {
                config_set_str(cfg, "access_jwt",  state->session.access_jwt.c_str());
                config_set_str(cfg, "refresh_jwt", state->session.refresh_jwt.c_str());
                config_set_str(cfg, "pds_url",     state->session.pds_url.c_str());
                config_save(cfg);
                config_free(cfg);
            }
        },
        [](const std::string&) {});
    return ok ? 0 : -1;
}

void app_switch_view(app_state_t *state, app_view_t view) {
    if (!state) return;
    state->current_view = view;

    switch (view) {
        case VIEW_LOGIN:
            login_view_draw(state);
            break;
        case VIEW_FEED:
            if (state->feed.items.empty())
                feed_view_refresh(state);
            else
                feed_view_draw(state);
            break;
        case VIEW_THREAD:
            state->thread_scroll = 0;
            thread_view_load(state);
            thread_view_draw(state);
            break;
        case VIEW_COMPOSE:
            compose_view_draw(state);
            break;
        case VIEW_SETTINGS:
            settings_view_draw(state);
            break;
    }
}

void app_run(app_state_t *state) {
    if (!state) return;

    fprintf(stderr, "app_run: drawing initial view=%d\n", (int)state->current_view);
    app_switch_view(state, state->current_view);
    fprintf(stderr, "app_run: initial view draw complete\n");

    input_event_t ev{};
    while (state->running) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        /* If async images arrived, repaint the current view. */
        if (image_cache_redraw_needed())
            app_switch_view(state, state->current_view);

        if (!state->input) {
            struct timespec ts = { 0, 50000000 };
            nanosleep(&ts, NULL);
            continue;
        }

        bool got_ev = input_poll(state->input, &ev, 100);
        if (!got_ev) continue;

        if (ev.type == INPUT_KEY &&
            (ev.key == KEY_POWER || ev.key == KEY_BACK || ev.key == KEY_HOME)) {
            state->running = false;
            break;
        }

        switch (state->current_view) {
            case VIEW_LOGIN:    login_view_handle(state, &ev);    break;
            case VIEW_FEED:     feed_view_handle(state, &ev);     break;
            case VIEW_THREAD:   thread_view_handle(state, &ev);   break;
            case VIEW_COMPOSE:  compose_view_handle(state, &ev);  break;
            case VIEW_SETTINGS: settings_view_handle(state, &ev); break;
        }
    }
}
