#include "atproto_client.h"

#include <client.h>
#include <xrpc_client.h>
#include <post_master.h>
#include <lexicon/com_atproto_server.h>
#include <lexicon/app_bsky_feed.h>
#include <lexicon/app_bsky_actor.h>
#include <lexicon/app_bsky_embed.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>

namespace Bsky {

static Author convertAuthor(const ATProto::AppBskyActor::ProfileViewBasic::SharedPtr& a) {
    if (!a) return {};
    Author au;
    au.did          = a->mDid.toStdString();
    au.handle       = a->mHandle.toStdString();
    au.display_name = a->mDisplayName.value_or(QString{}).toStdString();
    au.avatar_url   = a->mAvatar.value_or(QString{}).toStdString();
    return au;
}

static Post convertPostView(const ATProto::AppBskyFeed::PostView::SharedPtr& pv) {
    Post p;
    if (!pv) return p;
    p.uri    = pv->mUri.toStdString();
    p.cid    = pv->mCid.toStdString();
    p.author = convertAuthor(pv->mAuthor);

    if (pv->mRecordType == ATProto::RecordType::APP_BSKY_FEED_POST) {
        auto* rec = std::get_if<ATProto::AppBskyFeed::Record::Post::SharedPtr>(&pv->mRecord);
        if (rec && *rec) {
            p.text = (*rec)->mText.toStdString();
            if ((*rec)->mReply) {
                p.reply_parent_uri = (*rec)->mReply->mParent->mUri.toStdString();
                p.reply_parent_cid = (*rec)->mReply->mParent->mCid.toStdString();
                p.reply_root_uri   = (*rec)->mReply->mRoot->mUri.toStdString();
                p.reply_root_cid   = (*rec)->mReply->mRoot->mCid.toStdString();
            }
        }
    }

    p.indexed_at   = (std::time_t)pv->mIndexedAt.toSecsSinceEpoch();
    p.like_count   = pv->mLikeCount;
    p.reply_count  = pv->mReplyCount;
    p.repost_count = pv->mRepostCount;

    if (pv->mViewer) {
        if (pv->mViewer->mLike)   p.viewer_like   = pv->mViewer->mLike->toStdString();
        if (pv->mViewer->mRepost) p.viewer_repost = pv->mViewer->mRepost->toStdString();
    }

    if (pv->mEmbed) {
        if (pv->mEmbed->mType == ATProto::AppBskyEmbed::EmbedViewType::IMAGES_VIEW) {
            p.embed_type = EmbedType::Image;
            auto* iv = std::get_if<ATProto::AppBskyEmbed::ImagesView::SharedPtr>(&pv->mEmbed->mEmbed);
            if (iv && *iv) {
                for (auto& img : (*iv)->mImages)
                    p.image_urls.push_back(img->mThumb.toStdString());
            }
        } else if (pv->mEmbed->mType == ATProto::AppBskyEmbed::EmbedViewType::RECORD_VIEW) {
            p.embed_type = EmbedType::Quote;
            auto* rv = std::get_if<ATProto::AppBskyEmbed::RecordView::SharedPtr>(&pv->mEmbed->mEmbed);
            if (rv && *rv) {
                if ((*rv)->mRecordType == ATProto::RecordType::APP_BSKY_EMBED_RECORD_VIEW_RECORD) {
                    auto* vrr = std::get_if<ATProto::AppBskyEmbed::RecordViewRecord::SharedPtr>(&(*rv)->mRecord);
                    if (vrr && *vrr) {
                        auto qp = std::make_shared<Post>();
                        qp->uri    = (*vrr)->mUri.toStdString();
                        qp->cid    = (*vrr)->mCid.toStdString();
                        qp->author = convertAuthor((*vrr)->mAuthor);
                        if ((*vrr)->mValueType == ATProto::RecordType::APP_BSKY_FEED_POST) {
                            auto* vpost = std::get_if<ATProto::AppBskyFeed::Record::Post::SharedPtr>(&(*vrr)->mValue);
                            if (vpost && *vpost) qp->text = (*vpost)->mText.toStdString();
                        }
                        p.quoted_post = std::move(qp);
                    }
                }
            }
        } else if (pv->mEmbed->mType == ATProto::AppBskyEmbed::EmbedViewType::EXTERNAL_VIEW) {
            p.embed_type = EmbedType::External;
            auto* ev = std::get_if<ATProto::AppBskyEmbed::ExternalView::SharedPtr>(&pv->mEmbed->mEmbed);
            if (ev && *ev && (*ev)->mExternal) {
                p.ext_uri         = (*ev)->mExternal->mUri.toStdString();
                p.ext_title       = (*ev)->mExternal->mTitle.toStdString();
                p.ext_description = (*ev)->mExternal->mDescription.toStdString();
                p.ext_thumb_url   = (*ev)->mExternal->mThumb.value_or(QString{}).toStdString();
            }
        } else if (pv->mEmbed->mType == ATProto::AppBskyEmbed::EmbedViewType::RECORD_WITH_MEDIA_VIEW) {
            p.embed_type = EmbedType::RecordWithMedia;
            auto* rmv = std::get_if<ATProto::AppBskyEmbed::RecordWithMediaView::SharedPtr>(&pv->mEmbed->mEmbed);
            if (rmv && *rmv) {
                if ((*rmv)->mMediaType == ATProto::AppBskyEmbed::EmbedViewType::IMAGES_VIEW) {
                    auto* iv2 = std::get_if<ATProto::AppBskyEmbed::ImagesView::SharedPtr>(&(*rmv)->mMedia);
                    if (iv2 && *iv2) {
                        for (auto& img : (*iv2)->mImages)
                            p.image_urls.push_back(img->mThumb.toStdString());
                    }
                }
            }
        }
    }
    return p;
}

AtprotoClient::AtprotoClient(const std::string& serviceHost, QObject* parent)
    : QObject(parent), mHost(serviceHost.empty() ? DEFAULT_SERVICE_HOST : serviceHost) {
    auto xrpc = std::make_unique<Xrpc::Client>(QString::fromStdString(mHost));
    mClient = std::make_unique<ATProto::Client>(std::move(xrpc));
}

AtprotoClient::~AtprotoClient() = default;

void AtprotoClient::changeHost(const std::string& newHost) {
    if (newHost.empty() || newHost == mHost) return;
    mHost = newHost;
    auto xrpc = std::make_unique<Xrpc::Client>(QString::fromStdString(mHost));
    /* Preserve the existing session when switching hosts. */
    auto oldSession = mClient->getSession();
    mClient = std::make_unique<ATProto::Client>(std::move(xrpc));
    if (oldSession) mClient->resumeSession(*oldSession, nullptr, nullptr);
}

void AtprotoClient::createSession(const std::string& identifier, const std::string& password,
                                   const SessionCb& successCb, const ErrorCb& errorCb) {
    QEventLoop loop;
    mClient->createSession(
        QString::fromStdString(identifier),
        QString::fromStdString(password),
        std::nullopt, // authFactorToken (2FA)
        [this, &loop, successCb]() {
            auto* s = mClient->getSession();
            Session sess;
            sess.access_jwt   = s->mAccessJwt.toStdString();
            sess.refresh_jwt  = s->mRefreshJwt.toStdString();
            sess.did          = s->mDid.toStdString();
            sess.handle       = s->mHandle.toStdString();

            auto pds = s->getPDS();
            if (pds) {
                sess.pds_url = pds->toStdString();
                changeHost(sess.pds_url);
            }
            if (sess.pds_url.empty())
                sess.pds_url = mHost;

            successCb(sess);
            loop.quit();
        },
        [&loop, errorCb](const QString& err, const QString& msg) {
            errorCb((err + ": " + msg).toStdString());
            loop.quit();
        });
    loop.exec();
}

void AtprotoClient::resumeSession(const Session& session,
                                   const SuccessCb& successCb, const ErrorCb& errorCb) {
    auto sdkSession = std::make_shared<ATProto::ComATProtoServer::Session>();
    sdkSession->mAccessJwt  = QString::fromStdString(session.access_jwt);
    sdkSession->mRefreshJwt = QString::fromStdString(session.refresh_jwt);
    sdkSession->mDid        = QString::fromStdString(session.did);
    sdkSession->mHandle     = QString::fromStdString(session.handle);

    QEventLoop loop;
    mClient->resumeSession(*sdkSession,
        [&loop, successCb]() { successCb(); loop.quit(); },
        [&loop, errorCb](const QString& err, const QString& msg) {
            errorCb((err + ": " + msg).toStdString());
            loop.quit();
        });
    loop.exec();
}

void AtprotoClient::getTimeline(int limit, const std::optional<std::string>& cursor,
                                 const FeedCb& successCb, const ErrorCb& errorCb) {
    std::optional<QString> sdkCursor;
    if (cursor) sdkCursor = QString::fromStdString(*cursor);

    QEventLoop loop;
    mClient->getTimeline(limit, sdkCursor,
        [&loop, successCb](ATProto::AppBskyFeed::OutputFeed::SharedPtr output) {
            Feed feed;
            if (output) {
                feed.cursor = output->mCursor.value_or(QString{}).toStdString();
                for (auto& item : output->mFeed) {
                    if (!item || !item->mPost) continue;
                    Post p = convertPostView(item->mPost);
                    if (item->mReason &&
                        item->mReason->mType == ATProto::AppBskyFeed::FeedViewPostReasonType::REASON_REPOST) {
                        auto* rr = std::get_if<ATProto::AppBskyFeed::ReasonRepost::SharedPtr>(&item->mReason->mReason);
                        if (rr && *rr && (*rr)->mBy)
                            p.reposted_by = (*rr)->mBy->mHandle.toStdString();
                    }
                    feed.items.push_back(p);
                }
            }
            successCb(feed);
            loop.quit();
        },
        [&loop, errorCb](const QString& err, const QString& msg) {
            errorCb((err + ": " + msg).toStdString());
            loop.quit();
        });
    loop.exec();
}

static void flattenReplies(const ATProto::AppBskyFeed::ThreadViewPost::SharedPtr& tvp,
                           Post& post, int depth) {
    if (!tvp || depth > 10) return;
    for (auto& reply : tvp->mReplies) {
        if (!reply) continue;
        if (reply->mType != ATProto::AppBskyFeed::PostElementType::THREAD_VIEW_POST) continue;
        auto* rtvp = std::get_if<ATProto::AppBskyFeed::ThreadViewPost::SharedPtr>(&reply->mPost);
        if (!rtvp || !*rtvp || !(*rtvp)->mPost) continue;
        auto child = std::make_shared<Post>(convertPostView((*rtvp)->mPost));
        flattenReplies(*rtvp, *child, depth + 1);
        post.replies.push_back(std::move(child));
    }
}

void AtprotoClient::getPostThread(const std::string& uri,
                                   const PostCb& successCb, const ErrorCb& errorCb) {
    QEventLoop loop;
    mClient->getPostThread(
        QString::fromStdString(uri), std::nullopt, std::nullopt,
        [&loop, successCb, errorCb](ATProto::AppBskyFeed::PostThread::SharedPtr thread) {
            if (thread->mThread &&
                thread->mThread->mType == ATProto::AppBskyFeed::PostElementType::THREAD_VIEW_POST) {
                auto* tvp = std::get_if<ATProto::AppBskyFeed::ThreadViewPost::SharedPtr>(&thread->mThread->mPost);
                if (tvp && *tvp && (*tvp)->mPost) {
                    Post root = convertPostView((*tvp)->mPost);
                    flattenReplies(*tvp, root, 0);
                    successCb(root);
                    loop.quit();
                    return;
                }
            }
            errorCb("Post not found or blocked");
            loop.quit();
        },
        [&loop, errorCb](const QString& err, const QString& msg) {
            errorCb((err + ": " + msg).toStdString());
            loop.quit();
        });
    loop.exec();
}

bool AtprotoClient::hasSession() const {
    return mClient && mClient->getSession() != nullptr;
}

void AtprotoClient::likePost(const std::string& uri, const std::string& cid,
                              const LikeCb& successCb, const ErrorCb& errorCb) {
    ATProto::PostMaster pm(*mClient);
    QEventLoop loop;
    pm.like(QString::fromStdString(uri), QString::fromStdString(cid),
        [&loop, successCb](const QString& like_uri, const QString&) {
            successCb(like_uri.toStdString());
            loop.quit();
        },
        [&loop, errorCb](const QString& err, const QString& msg) {
            errorCb((err + ": " + msg).toStdString());
            loop.quit();
        });
    loop.exec();
}

void AtprotoClient::unlikePost(const std::string& like_uri,
                                const SuccessCb& successCb, const ErrorCb& errorCb) {
    ATProto::PostMaster pm(*mClient);
    QEventLoop loop;
    pm.undo(QString::fromStdString(like_uri),
        [&loop, successCb]() { successCb(); loop.quit(); },
        [&loop, errorCb](const QString& err, const QString& msg) {
            errorCb((err + ": " + msg).toStdString());
            loop.quit();
        });
    loop.exec();
}

void AtprotoClient::repostPost(const std::string& uri, const std::string& cid,
                                const LikeCb& successCb, const ErrorCb& errorCb) {
    ATProto::PostMaster pm(*mClient);
    QEventLoop loop;
    pm.repost(QString::fromStdString(uri), QString::fromStdString(cid),
        [&loop, successCb](const QString& repost_uri, const QString&) {
            successCb(repost_uri.toStdString());
            loop.quit();
        },
        [&loop, errorCb](const QString& err, const QString& msg) {
            errorCb((err + ": " + msg).toStdString());
            loop.quit();
        });
    loop.exec();
}

void AtprotoClient::unrepostPost(const std::string& repost_uri,
                                  const SuccessCb& successCb, const ErrorCb& errorCb) {
    ATProto::PostMaster pm(*mClient);
    QEventLoop loop;
    pm.undo(QString::fromStdString(repost_uri),
        [&loop, successCb]() { successCb(); loop.quit(); },
        [&loop, errorCb](const QString& err, const QString& msg) {
            errorCb((err + ": " + msg).toStdString());
            loop.quit();
        });
    loop.exec();
}

} // namespace Bsky
