#pragma once
#include <ctime>
#include <string>
#include <vector>
#include <memory>

namespace Bsky {

struct Author {
    std::string did;
    std::string handle;
    std::string display_name;
    std::string avatar_url;
};

enum class EmbedType { None, Image, Quote, RecordWithMedia, External, Video };

struct Post {
    std::string uri;
    std::string cid;
    Author      author;
    std::string text;
    std::time_t indexed_at = 0;
    int like_count   = 0;
    int reply_count  = 0;
    int repost_count = 0;
    EmbedType embed_type = EmbedType::None;
    std::shared_ptr<Post> quoted_post;
    std::vector<std::string> image_urls;
    std::vector<std::string> image_alts;
    std::string ext_uri, ext_title, ext_description, ext_thumb_url;
    std::string media_preview_url;
    std::string media_alt_text;
    std::string media_label;
    std::string reply_parent_uri, reply_parent_cid;
    std::string reply_root_uri,   reply_root_cid;
    std::vector<std::shared_ptr<Post>> replies;
    std::string viewer_like;    /* AT-URI of this user's like record, or empty */
    std::string viewer_repost;  /* AT-URI of this user's repost record, or empty */
    std::string reposted_by;    /* handle of reposter, or empty if not a repost */
};

struct Feed {
    std::vector<Post> items;
    std::string       cursor;
};

struct Session {
    std::string access_jwt;
    std::string refresh_jwt;
    std::string did;
    std::string handle;
    std::string display_name;
    std::string avatar_url;
    std::string pds_url;        /* Actual PDS endpoint (from DID doc or user override). */
    std::string appview_url;    /* AppView endpoint for feed/thread queries (from login.txt or default). */
};

} // namespace Bsky
