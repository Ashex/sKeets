#pragma once

#include "atproto/atproto.h"

#include <string>
#include <vector>

enum class skeets_bootstrap_state_t {
    waiting_for_login,
    session_restored,
    login_succeeded,
    error,
};

struct skeets_bootstrap_result_t {
    skeets_bootstrap_state_t state = skeets_bootstrap_state_t::waiting_for_login;
    Bsky::Session session;
    std::string headline;
    std::string detail;
    std::string error_message;
    bool authenticated = false;
    bool used_saved_session = false;
    bool consumed_login_file = false;
};

skeets_bootstrap_result_t skeets_run_bootstrap();
std::vector<std::string> skeets_bootstrap_lines(const skeets_bootstrap_result_t& result);