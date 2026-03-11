#pragma once

#include "atproto/atproto.h"

#include <string>
#include <vector>

enum class rewrite_feed_state_t {
    idle,
    loading,
    loaded,
    error,
};

struct rewrite_feed_result_t {
    rewrite_feed_state_t state = rewrite_feed_state_t::idle;
    Bsky::Feed feed;
    std::string error_message;
    int post_count = 0;
};

/// Fetch the home timeline for an authenticated session.
/// Creates a fresh AtprotoClient internally (synchronous, blocks via QEventLoop).
rewrite_feed_result_t rewrite_fetch_feed(const Bsky::Session& session,
                                          int limit = 30,
                                          const std::string& cursor = {});
