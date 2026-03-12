#include "rewrite/bootstrap.h"

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

bool resume_saved_session_with_retry(const Bsky::Session& session, std::string& error_message) {
    const std::string host = session.pds_url.empty() ? Bsky::DEFAULT_SERVICE_HOST : session.pds_url;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        bool resumed = false;
        error_message.clear();
        Bsky::AtprotoClient client(host);
        client.resumeSession(session,
                             [&resumed]() { resumed = true; },
                             [&error_message](const std::string& error) { error_message = error; });
        if (resumed) {
            return true;
        }

        std::fprintf(stderr,
                     "rewrite bootstrap: session resume attempt %d failed: %s\n",
                     attempt,
                     error_message.c_str());
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

struct rewrite_login_txt_t {
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
    Bsky::Session session;
    config_t* config = config_open(skeets_config_path());
    if (!config) return session;

    session.handle = config_get_str(config, "handle", "");
    session.access_jwt = config_get_str(config, "access_jwt", "");
    session.refresh_jwt = config_get_str(config, "refresh_jwt", "");
    session.did = config_get_str(config, "did", "");
    session.pds_url = config_get_str(config, "pds_url", "");
    session.appview_url = config_get_str(config,
                                         "appview_url",
                                         config_get_str(config, "appview", kDefaultAppView));
    config_free(config);
    return session;
}

bool has_saved_session(const Bsky::Session& session) {
    return !session.handle.empty() && !session.access_jwt.empty() && !session.refresh_jwt.empty() && !session.did.empty();
}

void save_session(const Bsky::Session& session) {
    config_t* config = config_open(skeets_config_path());
    if (!config) return;

    config_set_str(config, "handle", session.handle.c_str());
    config_set_str(config, "access_jwt", session.access_jwt.c_str());
    config_set_str(config, "refresh_jwt", session.refresh_jwt.c_str());
    config_set_str(config, "did", session.did.c_str());
    config_set_str(config, "pds_url", session.pds_url.c_str());
    config_set_str(config, "appview_url", session.appview_url.c_str());
    config_save(config);
    config_free(config);
}

rewrite_login_txt_t read_login_txt(bool& found_file) {
    rewrite_login_txt_t login;
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

rewrite_bootstrap_result_t make_waiting_result() {
    rewrite_bootstrap_result_t result;
    result.state = rewrite_bootstrap_state_t::waiting_for_login;
    result.headline = "Waiting for rewrite login.txt";
    result.detail = "Create /mnt/onboard/.adds/sKeets-rewrite/login.txt, then relaunch or tap Recheck.";
    return result;
}

rewrite_bootstrap_result_t make_error_result(const std::string& error_message, bool consumed_login_file) {
    rewrite_bootstrap_result_t result;
    result.state = rewrite_bootstrap_state_t::error;
    result.headline = "Authentication bootstrap failed";
    result.detail = "Fix login.txt or remove invalid session state, then relaunch or tap Recheck.";
    result.error_message = error_message;
    result.consumed_login_file = consumed_login_file;
    return result;
}

} // namespace

rewrite_bootstrap_result_t rewrite_run_bootstrap() {
    if (skeets_ensure_data_dirs() != 0) {
        return make_error_result("Failed to create rewrite data directories", false);
    }

    const Bsky::Session saved_session = load_saved_session();
    std::string saved_session_error;
    if (has_saved_session(saved_session)) {
        std::fprintf(stderr,
                 "rewrite bootstrap: attempting session resume for handle=%s pds=%s\n",
                 saved_session.handle.c_str(),
                 saved_session.pds_url.c_str());

        if (resume_saved_session_with_retry(saved_session, saved_session_error)) {
            rewrite_bootstrap_result_t result;
            result.state = rewrite_bootstrap_state_t::session_restored;
            result.session = saved_session;
            result.headline = "Saved session restored";
            result.detail = "Authentication is valid. Loading the home timeline.";
            result.authenticated = true;
            result.used_saved_session = true;
            return result;
        }

        std::fprintf(stderr, "rewrite bootstrap: session resume failed after retries: %s\n", saved_session_error.c_str());
    }

    bool found_login_file = false;
    const rewrite_login_txt_t login = read_login_txt(found_login_file);
    if (!found_login_file) {
        if (has_saved_session(saved_session)) {
            return make_error_result(saved_session_error.empty() ? "Saved session resume failed" : saved_session_error, false);
        }
        return make_waiting_result();
    }

    if (login.handle.empty() || login.password.empty()) {
        return make_error_result("login.txt is missing required handle or password fields", true);
    }

    std::fprintf(stderr,
                 "rewrite bootstrap: attempting createSession handle=%s pds=%s appview=%s\n",
                 login.handle.c_str(),
                 login.pds_url.empty() ? Bsky::DEFAULT_SERVICE_HOST : login.pds_url.c_str(),
                 login.appview_url.c_str());

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

    created_session.appview_url = login.appview_url.empty() ? kDefaultAppView : login.appview_url;
    save_session(created_session);

    rewrite_bootstrap_result_t result;
    result.state = rewrite_bootstrap_state_t::login_succeeded;
    result.session = created_session;
    result.headline = "Login completed";
    result.detail = "Session tokens were saved under the rewrite app root.";
    result.authenticated = true;
    result.consumed_login_file = true;
    return result;
}

std::vector<std::string> rewrite_bootstrap_lines(const rewrite_bootstrap_result_t& result) {
    if (result.authenticated) {
        return {
            "State: " + std::string(result.used_saved_session ? "saved session restored" : "login.txt sign-in completed"),
            "Handle: " + (result.session.handle.empty() ? std::string("unknown") : result.session.handle),
            "DID: " + (result.session.did.empty() ? std::string("unknown") : result.session.did),
            "PDS: " + (result.session.pds_url.empty() ? std::string(Bsky::DEFAULT_SERVICE_HOST) : result.session.pds_url),
            "AppView: " + (result.session.appview_url.empty() ? std::string(kDefaultAppView) : result.session.appview_url),
            "Config: /mnt/onboard/.adds/sKeets-rewrite/config.ini",
        };
    }

    std::vector<std::string> lines{
        "1. Connect the Kobo over USB.",
        "2. Create /mnt/onboard/.adds/sKeets-rewrite/login.txt.",
        "3. Add handle=you.bsky.social and password=xxxx-xxxx-xxxx-xxxx.",
        "4. Optional: pds_url=https://pds.example and appview=https://api.bsky.app.",
        "5. Relaunch the rewrite app or tap Recheck.",
    };

    if (!result.error_message.empty()) {
        lines.push_back("Last error: " + result.error_message);
    }
    return lines;
}