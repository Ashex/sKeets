#include "kobo/bootstrap.h"

#include "atproto/atproto_client.h"
#include "util/config.h"
#include "util/paths.h"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char* kDefaultAppView = "https://api.bsky.app";

bool looks_like_transient_network_error(const std::string& error_message) {
    return error_message.find("Host ") != std::string::npos ||
           error_message.find("Network") != std::string::npos ||
           error_message.find("timed out") != std::string::npos ||
           error_message.find("Temporary failure") != std::string::npos;
}

bool restore_saved_session_with_retry(const Bsky::Session& session,
                                      Bsky::Session& restored_session,
                                      std::string& error_message) {
    const std::string host = session.pds_url.empty() ? Bsky::DEFAULT_SERVICE_HOST : session.pds_url;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        bool restored = false;
        error_message.clear();
        Bsky::AtprotoClient client(host);
        client.restoreSession(session,
                              [&restored, &restored_session](const Bsky::Session& refreshed_session) {
                                  restored = true;
                                  restored_session = refreshed_session;
                              },
                              [&error_message](const std::string& error) { error_message = error; });
        if (restored) {
            return true;
        }

        if (attempt == 3 || !looks_like_transient_network_error(error_message)) {
            return false;
        }
        usleep(500000);
    }
    return false;
}

std::string normalize_url(std::string value) {
    if (value.empty()) return value;
    if (value.find("://") == std::string::npos) {
        value = "https://" + value;
    }
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

struct skeets_login_txt_t {
    std::string handle;
    std::string password;
    std::string pds_url;
    std::string appview_url;
};

bool file_exists(const char* path) {
    struct stat st {};
    return path && stat(path, &st) == 0;
}

Bsky::Session load_saved_session() {
    const char* config_path = skeets_config_path();
    std::fprintf(stderr, "bootstrap: load_saved_session: config_path='%s'\n", config_path);

    Bsky::Session session;
    config_t* config = config_open(config_path);
    if (!config) {
        std::fprintf(stderr, "bootstrap: load_saved_session: config_open returned NULL\n");
        return session;
    }

    session.handle = config_get_str(config, "handle", "");
    session.access_jwt = config_get_str(config, "access_jwt", "");
    session.refresh_jwt = config_get_str(config, "refresh_jwt", "");
    session.did = config_get_str(config, "did", "");
    session.pds_url = config_get_str(config, "pds_url", "");
    session.appview_url = config_get_str(config,
                                         "appview_url",
                                         config_get_str(config, "appview", kDefaultAppView));
    
    std::fprintf(stderr, "bootstrap: load_saved_session: loaded handle='%s' did='%s' pds='%s' has_access=%zu has_refresh=%zu\n",
                 session.handle.empty() ? "(empty)" : session.handle.c_str(),
                 session.did.empty() ? "(empty)" : session.did.c_str(),
                 session.pds_url.empty() ? "(empty)" : session.pds_url.c_str(),
                 session.access_jwt.size(),
                 session.refresh_jwt.size());
    
    config_free(config);
    return session;
}

bool has_saved_session(const Bsky::Session& session) {
    return !session.handle.empty() && !session.access_jwt.empty() && !session.refresh_jwt.empty() && !session.did.empty();
}

void save_session(const Bsky::Session& session) {
    const char* config_path = skeets_config_path();

    if (session.handle.empty() && session.access_jwt.empty() && session.did.empty()) {
        std::fprintf(stderr, "bootstrap: save_session: SKIPPING — session is empty (would overwrite valid config)\n");
        return;
    }

    std::fprintf(stderr, "bootstrap: save_session: saving handle='%s' did='%s' pds='%s' access_len=%zu refresh_len=%zu\n",
                 session.handle.c_str(), session.did.c_str(), session.pds_url.c_str(),
                 session.access_jwt.size(), session.refresh_jwt.size());

    config_t* config = config_open(config_path);
    if (!config) {
        std::fprintf(stderr, "bootstrap: save_session: config_open returned NULL for '%s'\n", config_path);
        return;
    }

    config_set_str(config, "handle", session.handle.c_str());
    config_set_str(config, "access_jwt", session.access_jwt.c_str());
    config_set_str(config, "refresh_jwt", session.refresh_jwt.c_str());
    config_set_str(config, "did", session.did.c_str());
    config_set_str(config, "pds_url", session.pds_url.c_str());
    config_set_str(config, "appview_url", session.appview_url.c_str());

    int result = config_save(config);
    if (result != 0) {
        std::fprintf(stderr, "bootstrap: save_session: config_save FAILED for '%s' (result=%d)\n", config_path, result);
    } else {
        std::fprintf(stderr, "bootstrap: save_session: config_save succeeded for '%s'\n", config_path);
    }
    config_free(config);
}

skeets_login_txt_t read_login_txt(bool& found_file) {
    skeets_login_txt_t login;
    found_file = file_exists(skeets_login_txt_path());
    if (!found_file) return login;

    config_t* config = config_open(skeets_login_txt_path());
    if (!config) return login;

    login.handle = config_get_str(config, "handle", "");
    login.password = config_get_str(config, "password", "");
    login.pds_url = normalize_url(config_get_str(config, "pds_url", ""));
    login.appview_url = normalize_url(config_get_str(config, "appview", kDefaultAppView));
    config_free(config);

    unlink(skeets_login_txt_path());
    return login;
}

skeets_bootstrap_result_t make_waiting_result() {
    skeets_bootstrap_result_t result;
    result.state = skeets_bootstrap_state_t::waiting_for_login;
    result.headline = "Waiting for sKeets login.txt";
    result.detail = "Create /mnt/onboard/.adds/sKeets/login.txt, then relaunch or tap Recheck.";
    return result;
}

skeets_bootstrap_result_t make_error_result(const std::string& error_message, bool consumed_login_file) {
    skeets_bootstrap_result_t result;
    result.state = skeets_bootstrap_state_t::error;
    result.headline = "Authentication bootstrap failed";
    result.detail = "Fix login.txt or remove invalid session state, then relaunch or tap Recheck.";
    result.error_message = error_message;
    result.consumed_login_file = consumed_login_file;
    return result;
}

} // namespace

skeets_bootstrap_result_t skeets_run_bootstrap() {
    std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: starting\n");
    
    if (skeets_ensure_data_dirs() != 0) {
        std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: failed to create data dirs\n");
        return make_error_result("Failed to create rewrite data directories", false);
    }

    std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: loading saved session\n");
    Bsky::Session saved_session = load_saved_session();

    Bsky::Session restored_session;
    std::string saved_session_error;
    if (has_saved_session(saved_session)) {
        std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: found saved session, attempting restore\n");
        if (restore_saved_session_with_retry(saved_session, restored_session, saved_session_error)) {
            std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: session restore succeeded\n");
            save_session(restored_session);
            skeets_bootstrap_result_t result;
            result.state = skeets_bootstrap_state_t::session_restored;
            result.session = restored_session;
            result.headline = "Saved session restored";
            result.detail = "Authentication is valid. Loading the home timeline.";
            result.authenticated = true;
            result.used_saved_session = true;
            return result;
        }
        std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: session restore failed: %s\n", saved_session_error.c_str());
    } else {
        std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: no saved session found\n");
    }

    std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: checking for login.txt\n");
    bool found_login_file = false;
    const skeets_login_txt_t login = read_login_txt(found_login_file);
    if (!found_login_file) {
        std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: no login.txt found\n");
        if (has_saved_session(saved_session)) {
            std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: returning error (had session but restore failed)\n");
            return make_error_result(saved_session_error.empty() ? "Saved session resume failed" : saved_session_error, false);
        }
        std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: returning waiting for login\n");
        return make_waiting_result();
    }

    std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: login.txt found, handle='%s'\n", login.handle.c_str());
    if (login.handle.empty() || login.password.empty()) {
        std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: login.txt missing handle or password\n");
        return make_error_result("login.txt is missing required handle or password fields", true);
    }

    std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: attempting createSession\n");
    bool created = false;
    std::string error_message;
    Bsky::Session created_session;
    Bsky::AtprotoClient client(login.pds_url.empty() ? Bsky::DEFAULT_SERVICE_HOST : login.pds_url);
    client.createSession(login.handle,
                         login.password,
                         [&created, &created_session](const Bsky::Session& session) {
                             created = true;
                             created_session = session;
                         },
                         [&error_message](const std::string& error) { error_message = error; });
    if (!created) {
        std::fprintf(stderr, "rewrite bootstrap: createSession failed: %s\n", error_message.c_str());
        return make_error_result(error_message.empty() ? "Login failed" : error_message, true);
    }

    std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: createSession succeeded, saving session\n");
    created_session.appview_url = login.appview_url.empty() ? kDefaultAppView : login.appview_url;
    save_session(created_session);

    skeets_bootstrap_result_t result;
    result.state = skeets_bootstrap_state_t::login_succeeded;
    result.session = created_session;
    result.headline = "Login completed";
    result.detail = "Session tokens were saved under the rewrite app root.";
    result.authenticated = true;
    result.consumed_login_file = true;
    std::fprintf(stderr, "bootstrap: skeets_run_bootstrap: returning success\n");
    return result;
}

std::vector<std::string> skeets_bootstrap_lines(const skeets_bootstrap_result_t& result) {
    if (result.authenticated) {
        return {
            "State: " + std::string(result.used_saved_session ? "saved session restored" : "login.txt sign-in completed"),
            "Handle: " + (result.session.handle.empty() ? std::string("unknown") : result.session.handle),
            "DID: " + (result.session.did.empty() ? std::string("unknown") : result.session.did),
            "PDS: " + (result.session.pds_url.empty() ? std::string(Bsky::DEFAULT_SERVICE_HOST) : result.session.pds_url),
            "AppView: " + (result.session.appview_url.empty() ? std::string(kDefaultAppView) : result.session.appview_url),
            "Config: /mnt/onboard/.adds/sKeets/config.ini",
        };
    }

    std::vector<std::string> lines{
        "1. Connect the Kobo over USB.",
        "2. Create /mnt/onboard/.adds/sKeets/login.txt.",
        "3. Add handle=you.bsky.social and password=xxxx-xxxx-xxxx-xxxx.",
        "4. Optional: pds_url=https://pds.example and appview=https://api.bsky.app.",
        "5. Relaunch the rewrite app or tap Recheck.",
    };

    if (!result.error_message.empty()) {
        lines.push_back("Last error: " + result.error_message);
    }
    return lines;
}