#include "rewrite/actions.h"

#include "atproto/atproto_client.h"

#include <unistd.h>

namespace {

bool looks_like_transient_network_error(const std::string& error_message) {
    return error_message.find("Host ") != std::string::npos ||
           error_message.find("Network") != std::string::npos ||
           error_message.find("timed out") != std::string::npos ||
           error_message.find("Temporary failure") != std::string::npos;
}

bool session_changed(const Bsky::Session& before, const Bsky::Session& after) {
    return before.access_jwt != after.access_jwt ||
           before.refresh_jwt != after.refresh_jwt ||
           before.did != after.did ||
           before.handle != after.handle ||
           before.pds_url != after.pds_url;
}

template <typename Operation>
rewrite_action_result_t run_action_with_session(const Bsky::Session& session, Operation operation) {
    rewrite_action_result_t result;
    const std::string host = session.pds_url.empty()
        ? std::string(Bsky::DEFAULT_SERVICE_HOST)
        : session.pds_url;

    Bsky::AtprotoClient client(host);
    bool resumed = false;
    std::string resume_error;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        resumed = false;
        resume_error.clear();
        client.restoreSession(session,
                              [&result, &resumed, &session](const Bsky::Session& restored_session) {
                                  resumed = true;
                                  result.session = restored_session;
                                  result.session_updated = session_changed(session, restored_session);
                              },
                              [&resume_error](const std::string& error) { resume_error = error; });
        if (resumed) break;
        if (attempt == 3 || !looks_like_transient_network_error(resume_error)) break;
        usleep(500000);
    }

    if (!resumed) {
        result.error_message = "Session resume failed: " + resume_error;
        return result;
    }

    operation(client, result);
    return result;
}

} // namespace

rewrite_action_result_t rewrite_like_post(const Bsky::Session& session,
                                          const std::string& post_uri,
                                          const std::string& post_cid) {
    return run_action_with_session(session, [&](Bsky::AtprotoClient& client, rewrite_action_result_t& result) {
        client.likePost(post_uri,
                        post_cid,
                        [&result](const std::string& like_uri) {
                            result.ok = true;
                            result.record_uri = like_uri;
                        },
                        [&result](const std::string& error) {
                            result.error_message = error;
                        });
    });
}

rewrite_action_result_t rewrite_unlike_post(const Bsky::Session& session,
                                            const std::string& like_uri) {
    return run_action_with_session(session, [&](Bsky::AtprotoClient& client, rewrite_action_result_t& result) {
        client.unlikePost(like_uri,
                          [&result]() {
                              result.ok = true;
                          },
                          [&result](const std::string& error) {
                              result.error_message = error;
                          });
    });
}

rewrite_action_result_t rewrite_repost_post(const Bsky::Session& session,
                                            const std::string& post_uri,
                                            const std::string& post_cid) {
    return run_action_with_session(session, [&](Bsky::AtprotoClient& client, rewrite_action_result_t& result) {
        client.repostPost(post_uri,
                          post_cid,
                          [&result](const std::string& repost_uri) {
                              result.ok = true;
                              result.record_uri = repost_uri;
                          },
                          [&result](const std::string& error) {
                              result.error_message = error;
                          });
    });
}

rewrite_action_result_t rewrite_unrepost_post(const Bsky::Session& session,
                                              const std::string& repost_uri) {
    return run_action_with_session(session, [&](Bsky::AtprotoClient& client, rewrite_action_result_t& result) {
        client.unrepostPost(repost_uri,
                            [&result]() {
                                result.ok = true;
                            },
                            [&result](const std::string& error) {
                                result.error_message = error;
                            });
    });
}
