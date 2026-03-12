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

/// Fetch the home timeline for an authenticated session.
/// Creates a fresh AtprotoClient internally (synchronous, blocks via QEventLoop).
skeets_feed_result_t skeets_fetch_feed(const Bsky::Session& session,
                                          int limit = 30,
                                          const std::string& cursor = {});
