#pragma once

#include "atproto/atproto.h"

#include <string>

enum class skeets_thread_state_t {
    idle,
    loading,
    loaded,
    error,
};

struct skeets_thread_result_t {
    skeets_thread_state_t state = skeets_thread_state_t::idle;
    Bsky::Post root;
    Bsky::Session session;
    std::string error_message;
    bool session_updated = false;
};

skeets_thread_result_t skeets_fetch_thread(const Bsky::Session& session,
                                             const std::string& post_uri);
