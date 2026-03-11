#include "app.h"
#include "ui/auth_view.h"
#include "ui/feed_view.h"
#include "ui/thread_view.h"
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

static void save_session_config(const app_state_t *state) {
    if (!state) return;
    config_t *cfg = config_open(skeets_config_path());
    if (!cfg) return;
    config_set_str(cfg, "handle",      state->session.handle.c_str());
    config_set_str(cfg, "access_jwt",  state->session.access_jwt.c_str());
    config_set_str(cfg, "refresh_jwt", state->session.refresh_jwt.c_str());
    config_set_str(cfg, "did",         state->session.did.c_str());
    config_set_str(cfg, "pds_url",     state->session.pds_url.c_str());
    if (!state->session.appview_url.empty())
        config_set_str(cfg, "appview_url", state->session.appview_url.c_str());
    config_save(cfg);
    config_free(cfg);
}

static void clear_pending_login(app_state_t *state) {
    if (!state) return;
    state->pending_login = false;
    state->pending_handle.clear();
    state->pending_password.clear();
    state->pending_pds_url.clear();
    state->pending_appview_url.clear();
}

static void try_pending_login(app_state_t *state) {
    if (!state || !state->pending_login) return;

    fprintf(stderr, "app_run: attempting login.txt sign-in for %s\n",
            state->pending_handle.c_str());
    auth_view_set_error(nullptr);
    auth_view_set_info("Signing in...");
    auth_view_draw(state);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    if (!state->pending_pds_url.empty())
        state->atproto_client->changeHost(state->pending_pds_url);

    bool ok = false;
    std::string err_msg;
    state->atproto_client->createSession(state->pending_handle, state->pending_password,
        [state, &ok](const Bsky::Session& sess) {
            state->session = sess;
            ok = true;
        },
        [&err_msg](const std::string& e) {
            err_msg = e;
        });

    if (!ok) {
        fprintf(stderr, "app_run: login.txt sign-in failed: %s\n", err_msg.c_str());
        auth_view_set_info(nullptr);
        auth_view_set_error(err_msg.empty() ? "Login failed from login.txt" : err_msg.c_str());
        auth_view_draw(state);
        clear_pending_login(state);
        return;
    }

    state->session_last_refresh = time(nullptr);
    if (!state->pending_appview_url.empty())
        state->session.appview_url = state->pending_appview_url;
    save_session_config(state);
    remove(skeets_login_txt_path());

    auth_view_set_error(nullptr);
    auth_view_set_info("Login successful");
    auth_view_draw(state);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    fprintf(stderr, "app_run: login.txt sign-in succeeded; loading initial feed\n");
    clear_pending_login(state);
    app_set_info(state, "Loading feed...");
    auth_view_set_info(nullptr);
    app_switch_view(state, VIEW_FEED);
}

int app_init(app_state_t *state) {
    if (!state) return -1;
    *state = app_state_t{};
    state->running = true;
    state->current_view = VIEW_AUTH_WAIT;

    const std::string kobo_version = device_kobo_version_string();
    const std::string kobo_model = device_kobo_model_string();
    fprintf(stderr, "app_init: device model=%s version=%s\n",
            kobo_model.c_str(),
            kobo_version.empty() ? "unknown" : kobo_version.c_str());

    if (skeets_ensure_data_dirs() != 0) {
        fprintf(stderr, "app_init: warning: failed to prepare data directories under %s\n",
                skeets_data_dir());
    }

    image_evict_disk_cache(50UL * 1024 * 1024);

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

    state->input = input_open(state->fb.width, state->fb.height);
    if (!state->input)
        fprintf(stderr, "app_init: warning: failed to open input devices\n");
    else
        fprintf(stderr, "app_init: input devices opened\n");

    config_t *cfg = config_open(skeets_config_path());
    std::string saved_pds_url;
    if (cfg) {
        state->profile_images_enabled = config_get_bool(cfg, "profile_images_enabled", true);
        state->embed_images_enabled   = config_get_bool(cfg, "embed_images_enabled", false);
        saved_pds_url                 = config_get_str(cfg, "pds_url", "");
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

        const bool has_saved_session = saved_handle[0] && saved_access[0] && saved_did[0];
        if (has_saved_session) {
            state->session.handle      = saved_handle;
            state->session.access_jwt  = saved_access;
            state->session.refresh_jwt = saved_refresh;
            state->session.did         = saved_did;
            state->session.pds_url     = saved_pds_url;
            state->session.appview_url = config_get_str(cfg, "appview_url", "");

            bool session_resumed = false;
            state->atproto_client->resumeSession(state->session,
                [&session_resumed, state]() {
                    session_resumed = true;
                    state->session_last_refresh = time(nullptr);
                },
                [](const std::string&) {});
            if (session_resumed)
                state->current_view = VIEW_FEED;

        } else {
            // No saved session — check for login.txt
            const char *login_txt = skeets_login_txt_path();
            FILE *f = fopen(login_txt, "r");
            if (f) {
                char lt_handle[128]   = {};
                char lt_password[128] = {};
                char lt_pds_url[256]  = {};
                char lt_appview[256]  = {};

                char line[512];
                while (fgets(line, sizeof(line), f)) {
                    int len = (int)strlen(line);
                    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' '))
                        line[--len] = '\0';
                    char *eq = strchr(line, '=');
                    if (!eq) continue;
                    *eq = '\0';
                    const char *k = line;
                    const char *v = eq + 1;
                    if (strcmp(k, "handle")   == 0)      strncpy(lt_handle,   v, sizeof(lt_handle)-1);
                    else if (strcmp(k, "password") == 0) strncpy(lt_password, v, sizeof(lt_password)-1);
                    else if (strcmp(k, "pds_url")  == 0) strncpy(lt_pds_url,  v, sizeof(lt_pds_url)-1);
                    else if (strcmp(k, "appview")  == 0) strncpy(lt_appview,  v, sizeof(lt_appview)-1);
                }
                fclose(f);

                if (lt_handle[0] && lt_password[0]) {
                    state->pending_login = true;
                    state->pending_handle = lt_handle;
                    state->pending_password = lt_password;
                    state->pending_pds_url = lt_pds_url;
                    state->pending_appview_url = lt_appview;
                    auth_view_set_info("login.txt found; ready to sign in");
                    fprintf(stderr, "app_init: login.txt queued for sign-in\n");
                } else {
                    remove(login_txt);
                    auth_view_set_error("login.txt missing handle or password fields");
                }
            }
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

    /* Proactively refresh if token is >85 minutes old */
    time_t now = time(nullptr);
    const time_t REFRESH_THRESHOLD_SECS = 85 * 60;
    if (state->session_last_refresh > 0 &&
        (now - state->session_last_refresh) > REFRESH_THRESHOLD_SECS) {
        bool refreshed = false;
        state->atproto_client->resumeSession(state->session,
            [state, &refreshed, now]() {
                refreshed = true;
                state->session_last_refresh = now;
            },
            [](const std::string&) {});
        if (!refreshed) {
            app_switch_view(state, VIEW_AUTH_WAIT);
            return -1;
        }
    }

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
    if (!ok)
        app_switch_view(state, VIEW_AUTH_WAIT);
    return ok ? 0 : -1;
}

void app_switch_view(app_state_t *state, app_view_t view) {
    if (!state) return;
    state->current_view = view;

    switch (view) {
        case VIEW_AUTH_WAIT:
            auth_view_draw(state);
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

    if (state->pending_login) {
        try_pending_login(state);
    }

    input_event_t ev{};
    while (state->running) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        /* If async images arrived, repaint the current view. */
        if (image_cache_redraw_needed()) {
            switch (state->current_view) {
                case VIEW_FEED:     feed_view_draw(state);     break;
                case VIEW_THREAD:   thread_view_draw(state);   break;
                default: break;
            }
        }

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
            case VIEW_AUTH_WAIT: auth_view_handle(state, &ev);    break;
            case VIEW_FEED:      feed_view_handle(state, &ev);    break;
            case VIEW_THREAD:    thread_view_handle(state, &ev);  break;
            case VIEW_SETTINGS:  settings_view_handle(state, &ev); break;
        }
    }
}
