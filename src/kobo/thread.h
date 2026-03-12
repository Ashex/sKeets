#pragma once

#include "atproto/atproto.h"

#include <string>

enum class rewrite_thread_state_t {
    idle,
    loading,
    loaded,
    error,
};

struct rewrite_thread_result_t {
    rewrite_thread_state_t state = rewrite_thread_state_t::idle;
    Bsky::Post root;
    Bsky::Session session;
    std::string error_message;
    bool session_updated = false;
};

rewrite_thread_result_t rewrite_fetch_thread(const Bsky::Session& session,
                                             const std::string& post_uri);
