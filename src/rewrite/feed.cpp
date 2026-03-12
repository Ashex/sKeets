#include "rewrite/feed.h"

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

bool should_include_feed_post(const Bsky::Post& post) {
    if (!post.reply_parent_uri.empty() && post.reposted_by.empty()) {
        return false;
    }
    return true;
}

bool session_changed(const Bsky::Session& before, const Bsky::Session& after) {
    return before.access_jwt != after.access_jwt ||
           before.refresh_jwt != after.refresh_jwt ||
           before.did != after.did ||
           before.handle != after.handle ||
           before.pds_url != after.pds_url;
}

}

rewrite_feed_result_t rewrite_fetch_feed(const Bsky::Session& session,
                                          int limit,
                                          const std::string& cursor) {
    rewrite_feed_result_t result;
    result.state = rewrite_feed_state_t::loading;

    const std::string& pds = session.pds_url.empty()
        ? std::string(Bsky::DEFAULT_SERVICE_HOST)
        : session.pds_url;
    const std::string appview = session.appview_url.empty()
        ? std::string("https://api.bsky.app")
        : session.appview_url;

    std::fprintf(stderr,
                 "rewrite feed: fetching timeline from pds=%s appview=%s limit=%d cursor=%s\n",
                 pds.c_str(),
                 appview.c_str(),
                 limit,
                 cursor.empty() ? "(none)" : cursor.c_str());

    bool resumed = false;
    std::string resume_error;
    Bsky::AtprotoClient client(appview);
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
        result.state = rewrite_feed_state_t::error;
        result.error_message = "Session resume failed: " + resume_error;
        std::fprintf(stderr, "rewrite feed: %s\n", result.error_message.c_str());
        return result;
    }

    std::optional<std::string> cursor_opt;
    if (!cursor.empty()) cursor_opt = cursor;

    bool fetched = false;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        fetched = false;
        result.error_message.clear();
        client.getTimeline(limit, cursor_opt,
                           [&result, &fetched](const Bsky::Feed& feed) {
                               result.feed.cursor = feed.cursor;
                               result.feed.items.clear();
                               result.feed.items.reserve(feed.items.size());
                               for (const auto& post : feed.items) {
                                   if (should_include_feed_post(post)) {
                                       result.feed.items.push_back(post);
                                   }
                               }
                               result.post_count = static_cast<int>(result.feed.items.size());
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
        result.state = rewrite_feed_state_t::loaded;
        std::fprintf(stderr, "rewrite feed: loaded %d posts, cursor=%s\n",
                     result.post_count,
                     result.feed.cursor.empty() ? "(end)" : result.feed.cursor.c_str());
    } else {
        result.state = rewrite_feed_state_t::error;
        if (result.error_message.empty()) result.error_message = "getTimeline returned no data";
        std::fprintf(stderr, "rewrite feed: error: %s\n", result.error_message.c_str());
    }

    return result;
}
