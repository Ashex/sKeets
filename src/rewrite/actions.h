#pragma once

#include "atproto/atproto.h"

#include <string>

struct rewrite_action_result_t {
    bool ok = false;
    std::string record_uri;
    std::string error_message;
};

rewrite_action_result_t rewrite_like_post(const Bsky::Session& session,
                                          const std::string& post_uri,
                                          const std::string& post_cid);

rewrite_action_result_t rewrite_unlike_post(const Bsky::Session& session,
                                            const std::string& like_uri);

rewrite_action_result_t rewrite_repost_post(const Bsky::Session& session,
                                            const std::string& post_uri,
                                            const std::string& post_cid);

rewrite_action_result_t rewrite_unrepost_post(const Bsky::Session& session,
                                              const std::string& repost_uri);
