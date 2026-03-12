#pragma once

#include "atproto/atproto.h"

#include <string>

struct skeets_action_result_t {
    bool ok = false;
    Bsky::Session session;
    std::string record_uri;
    std::string error_message;
    bool session_updated = false;
};

skeets_action_result_t skeets_like_post(const Bsky::Session& session,
                                          const std::string& post_uri,
                                          const std::string& post_cid);

skeets_action_result_t skeets_unlike_post(const Bsky::Session& session,
                                            const std::string& like_uri);

skeets_action_result_t skeets_repost_post(const Bsky::Session& session,
                                            const std::string& post_uri,
                                            const std::string& post_cid);

skeets_action_result_t skeets_unrepost_post(const Bsky::Session& session,
                                              const std::string& repost_uri);
