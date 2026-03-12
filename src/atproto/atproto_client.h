#pragma once
#include "atproto.h"
#include <QObject>
#include <functional>
#include <optional>
#include <memory>

namespace ATProto { class Client; }
namespace Xrpc   { class Client; }

namespace Bsky {

/* Default Bluesky entryway – used when no PDS URL is provided. */
constexpr const char* DEFAULT_SERVICE_HOST = "https://bsky.social";

/* Default BCP-47 language tag used when creating posts. */
constexpr const char* DEFAULT_POST_LANGUAGE = "en";

class AtprotoClient : public QObject {
    Q_OBJECT
public:
    using SuccessCb     = std::function<void()>;
    using ErrorCb       = std::function<void(const std::string& error)>;
    using SessionCb     = std::function<void(const Session&)>;
    using FeedCb        = std::function<void(const Feed&)>;
    using PostCb        = std::function<void(const Post&)>;
    using LikeCb        = std::function<void(const std::string& like_uri)>;

    explicit AtprotoClient(const std::string& serviceHost = DEFAULT_SERVICE_HOST,
                           QObject* parent = nullptr);
    ~AtprotoClient() override;

    /* Re-point the XRPC client at a different PDS host (e.g. after DID-doc
       resolution reveals the user's actual PDS). */
    void changeHost(const std::string& newHost);

    void createSession(const std::string& identifier, const std::string& password,
                       const SessionCb& successCb, const ErrorCb& errorCb);

    void resumeSession(const Session& session,
                       const SuccessCb& successCb, const ErrorCb& errorCb);

    void restoreSession(const Session& session,
                        const SessionCb& successCb, const ErrorCb& errorCb);

    void getTimeline(int limit, const std::optional<std::string>& cursor,
                     const FeedCb& successCb, const ErrorCb& errorCb);

    void getPostThread(const std::string& uri,
                       const PostCb& successCb, const ErrorCb& errorCb);

    void likePost(const std::string& uri, const std::string& cid,
                  const LikeCb& successCb, const ErrorCb& errorCb);

    void unlikePost(const std::string& like_uri,
                    const SuccessCb& successCb, const ErrorCb& errorCb);

    void repostPost(const std::string& uri, const std::string& cid,
                    const LikeCb& successCb, const ErrorCb& errorCb);

    void unrepostPost(const std::string& repost_uri,
                      const SuccessCb& successCb, const ErrorCb& errorCb);

    bool hasSession() const;

signals:
    void sessionExpired();

private:
    std::string mHost;
    std::unique_ptr<ATProto::Client> mClient;
};

} // namespace Bsky
