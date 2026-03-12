#pragma once

#include "atproto/atproto.h"

#include <string>
#include <vector>

enum class skeets_feed_state_t {
    idle,
    loading,
    loaded,
    error,
};

struct skeets_feed_result_t {
    skeets_feed_state_t state = skeets_feed_state_t::idle;
    Bsky::Feed feed;
    Bsky::Session session;
    std::string error_message;
    int post_count = 0;
    bool session_updated = false;
};

/// Fetch a timeline, custom feed, or pinned list for an authenticated session.
/// Creates a fresh AtprotoClient internally (synchronous, blocks via QEventLoop).
skeets_feed_result_t skeets_fetch_feed(const Bsky::Session& session,
                                       const Bsky::FeedSource& source = {},
                                       int limit = 30,
                                       const std::string& cursor = {});

std::vector<Bsky::FeedSource> skeets_fetch_pinned_feeds(const Bsky::Session& session,
                                                        Bsky::Session* updated_session = nullptr,
                                                        bool* session_updated = nullptr,
                                                        std::string* error_message = nullptr);
