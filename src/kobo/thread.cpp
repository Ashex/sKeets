#include "kobo/thread.h"

#include "atproto/atproto_client.h"

#include <cstdio>
#include <unistd.h>

namespace {

bool looks_like_transient_network_error(const std::string& error_message) {
    return error_message.find("Host ") != std::string::npos ||
           error_message.find("Network") != std::string::npos ||
           error_message.find("timed out") != std::string::npos ||
           error_message.find("Temporary failure") != std::string::npos;
}

bool session_changed(const Bsky::Session& before, const Bsky::Session& after) {
    return before.access_jwt != after.access_jwt ||
           before.refresh_jwt != after.refresh_jwt ||
           before.did != after.did ||
           before.handle != after.handle ||
           before.pds_url != after.pds_url;
}

} // namespace

rewrite_thread_result_t rewrite_fetch_thread(const Bsky::Session& session,
                                             const std::string& post_uri) {
    rewrite_thread_result_t result;
    result.state = rewrite_thread_state_t::loading;

    if (post_uri.empty()) {
        result.state = rewrite_thread_state_t::error;
        result.error_message = "Post URI is empty";
        return result;
    }

    const std::string appview = session.appview_url.empty()
        ? std::string("https://api.bsky.app")
        : session.appview_url;

    std::fprintf(stderr,
                 "rewrite thread: fetching thread from appview=%s uri=%s\n",
                 appview.c_str(),
                 post_uri.c_str());

    Bsky::AtprotoClient client(appview);
    bool resumed = false;
    std::string resume_error;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        resumed = false;
        resume_error.clear();
        client.restoreSession(session,
                              [&result, &resumed, &session](const Bsky::Session& restored_session) {
                                  resumed = true;
                                  result.session = restored_session;
                                  result.session_updated = session_changed(session, restored_session);
                              },
                              [&resume_error](const std::string& e) { resume_error = e; });
        if (resumed) break;
        if (attempt == 3 || !looks_like_transient_network_error(resume_error)) break;
        usleep(500000);
    }

    if (!resumed) {
        result.state = rewrite_thread_state_t::error;
        result.error_message = "Session resume failed: " + resume_error;
        std::fprintf(stderr, "rewrite thread: %s\n", result.error_message.c_str());
        return result;
    }

    bool fetched = false;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        fetched = false;
        result.error_message.clear();
        client.getPostThread(post_uri,
                             [&result, &fetched](const Bsky::Post& root) {
                                 result.root = root;
                                 fetched = true;
                             },
                             [&result](const std::string& error) {
                                 result.error_message = error;
                             });
        if (fetched) break;
        if (attempt == 3 || !looks_like_transient_network_error(result.error_message)) break;
        usleep(500000);
    }

    if (fetched) {
        result.state = rewrite_thread_state_t::loaded;
        std::fprintf(stderr,
                     "rewrite thread: loaded thread uri=%s replies=%zu\n",
                     result.root.uri.c_str(),
                     result.root.replies.size());
    } else {
        result.state = rewrite_thread_state_t::error;
        if (result.error_message.empty()) result.error_message = "getPostThread returned no data";
        std::fprintf(stderr, "rewrite thread: error: %s\n", result.error_message.c_str());
    }

    return result;
}
