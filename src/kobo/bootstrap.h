#pragma once

#include "atproto/atproto.h"

#include <string>
#include <vector>

enum class rewrite_bootstrap_state_t {
    waiting_for_login,
    session_restored,
    login_succeeded,
    error,
};

struct rewrite_bootstrap_result_t {
    rewrite_bootstrap_state_t state = rewrite_bootstrap_state_t::waiting_for_login;
    Bsky::Session session;
    std::string headline;
    std::string detail;
    std::string error_message;
    bool authenticated = false;
    bool used_saved_session = false;
    bool consumed_login_file = false;
};

rewrite_bootstrap_result_t rewrite_run_bootstrap();
std::vector<std::string> rewrite_bootstrap_lines(const rewrite_bootstrap_result_t& result);