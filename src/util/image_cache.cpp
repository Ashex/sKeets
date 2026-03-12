#include "image_cache.h"
#include "image.h"
#include "str.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <climits>
#include <string>
#include <unordered_map>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QByteArray>

/* ── Entry states ─────────────────────────────────────────────────── */

enum class CacheState { Loading, Ready, Failed };

struct cache_entry_t {
    CacheState  state        = CacheState::Loading;
    image_t     img{};          /* valid when state == Ready */
    int         scale_w      = 0;
    int         scale_h      = 0;
    long long   last_accessed = 0; /* ms timestamp for LRU eviction */
    std::string url;               /* full URL for hash-collision guard */
};

/* ── Globals ──────────────────────────────────────────────────────── */

static std::unordered_map<unsigned long, cache_entry_t> s_cache;
static QNetworkAccessManager *s_nam = nullptr;
static bool s_redraw_needed = false;

#define MAX_CACHE_ENTRIES 128
static constexpr long long kFailedRetryDelayMs = 3000;

static std::string normalize_image_request_url(const char *url) {
    if (!url || !*url) return {};

    std::string normalized(url);
    const std::string prefix = "https://cdn.bsky.app/img/";
    if (normalized.rfind(prefix, 0) != 0) {
        return normalized;
    }

    const size_t query_pos = normalized.find_first_of("?#");
    const std::string suffix = query_pos == std::string::npos
        ? std::string{}
        : normalized.substr(query_pos);
    const std::string base = query_pos == std::string::npos
        ? normalized
        : normalized.substr(0, query_pos);

    if (base.ends_with("@jpeg") ||
        base.ends_with("@jpg") ||
        base.ends_with("@png") ||
        base.ends_with("@webp")) {
        return normalized;
    }

    return base + "@jpeg" + suffix;
}

static std::string summarize_url(const char *url) {
    if (!url) return "(null)";
    std::string text(url);
    if (text.size() <= 96) return text;
    return text.substr(0, 93) + "...";
}

/* ── Helpers ──────────────────────────────────────────────────────── */

static long long cache_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void evict_if_full() {
    if ((int)s_cache.size() < MAX_CACHE_ENTRIES) return;
    /* LRU eviction: remove the oldest-accessed non-loading entry. */
    auto lru_it = s_cache.end();
    long long oldest = LLONG_MAX;
    for (auto it = s_cache.begin(); it != s_cache.end(); ++it) {
        if (it->second.state != CacheState::Loading &&
            it->second.last_accessed < oldest) {
            oldest = it->second.last_accessed;
            lru_it = it;
        }
    }
    if (lru_it != s_cache.end()) {
        if (lru_it->second.img.pixels) free(lru_it->second.img.pixels);
        s_cache.erase(lru_it);
    }
}

static QNetworkAccessManager *get_nam() {
    if (!s_nam)
        s_nam = new QNetworkAccessManager();
    return s_nam;
}

/* ── Public API ───────────────────────────────────────────────────── */

const image_t *image_cache_lookup(const char *url, int scale_w, int scale_h) {
    if (!url || !*url) return nullptr;

    const std::string request_url = normalize_image_request_url(url);
    unsigned long key = str_hash(request_url.c_str());

    auto it = s_cache.find(key);
    if (it != s_cache.end()) {
        /* Guard against hash collision: verify the stored URL matches. */
        if (!it->second.url.empty() && it->second.url != request_url) {
            /* Collision — evict the conflicting entry and re-fetch. */
            if (it->second.img.pixels) free(it->second.img.pixels);
            s_cache.erase(it);
        } else {
            if (it->second.state == CacheState::Ready) {
                it->second.last_accessed = cache_now_ms();
                return &it->second.img;
            }

            if (it->second.state == CacheState::Failed) {
                const long long now = cache_now_ms();
                if (now - it->second.last_accessed < kFailedRetryDelayMs) {
                    return nullptr;
                }
                fprintf(stderr, "image cache: retrying failed url=%s\n",
                        summarize_url(request_url.c_str()).c_str());
                s_cache.erase(it);
            } else {
                return nullptr; /* Loading */
            }
        }
    }

    /* Start async download. */
    evict_if_full();

    cache_entry_t &entry = s_cache[key];
    entry.state         = CacheState::Loading;
    entry.scale_w       = scale_w;
    entry.scale_h       = scale_h;
    entry.url           = request_url;
    entry.last_accessed = cache_now_ms();

    /* Check disk cache first. */
    char cache_path[512];
    image_cache_path(request_url.c_str(), cache_path, sizeof(cache_path));

    image_t cached_img{};
    if (image_load_file(cache_path, &cached_img) == 0) {
        if (scale_w > 0 && scale_h > 0)
            image_scale_to_fit(&cached_img, scale_w, scale_h);
        entry.img           = cached_img;
        entry.state         = CacheState::Ready;
        entry.last_accessed = cache_now_ms();
        s_redraw_needed = true;
        fprintf(stderr,
                "image cache: disk hit url=%s size=%dx%d\n",
            summarize_url(request_url.c_str()).c_str(),
                cached_img.width,
                cached_img.height);
        return &entry.img;
    }

    /* No disk cache hit — download asynchronously. */
        QNetworkRequest request(QUrl(QString::fromUtf8(request_url.c_str())));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    fprintf(stderr,
            "image cache: fetch start url=%s scale=%dx%d\n",
            summarize_url(request_url.c_str()).c_str(),
            scale_w,
            scale_h);

    QNetworkReply *reply = get_nam()->get(request);

    /* Capture key + scale params + url for the callback. */
        std::string url_str(request_url);
    QObject::connect(reply, &QNetworkReply::finished, [reply, key, scale_w, scale_h, url_str]() {
        reply->deleteLater();

        auto cit = s_cache.find(key);
        if (cit == s_cache.end()) return; /* entry was evicted */

        if (reply->error() != QNetworkReply::NoError) {
            cit->second.state = CacheState::Failed;
            cit->second.last_accessed = cache_now_ms();
            fprintf(stderr,
                    "image cache: fetch failed url=%s error=%s\n",
                    summarize_url(url_str.c_str()).c_str(),
                    reply->errorString().toUtf8().constData());
            return;
        }

        QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            cit->second.state = CacheState::Failed;
            cit->second.last_accessed = cache_now_ms();
            fprintf(stderr,
                    "image cache: empty response url=%s\n",
                    summarize_url(url_str.c_str()).c_str());
            return;
        }

        /* Decode image via the image.h API. */
        image_t img{};
        if (image_decode_memory(reinterpret_cast<const uint8_t*>(data.constData()),
                                data.size(), &img) != 0) {
            cit->second.state = CacheState::Failed;
            cit->second.last_accessed = cache_now_ms();
            fprintf(stderr,
                    "image cache: decode failed url=%s bytes=%lld\n",
                    summarize_url(url_str.c_str()).c_str(),
                    static_cast<long long>(data.size()));
            return;
        }

        /* Pre-scale to target dimensions. */
        if (scale_w > 0 && scale_h > 0)
            image_scale_to_fit(&img, scale_w, scale_h);

        /* Write to disk cache. */
        image_write_disk_cache(url_str.c_str(), &img);

        cit->second.img           = img;
        cit->second.state         = CacheState::Ready;
        cit->second.last_accessed = cache_now_ms();
        s_redraw_needed   = true;
        fprintf(stderr,
            "image cache: ready url=%s size=%dx%d\n",
            summarize_url(url_str.c_str()).c_str(),
            img.width,
            img.height);
    });

    return nullptr;
}

bool image_cache_redraw_needed() {
    bool val = s_redraw_needed;
    s_redraw_needed = false;
    return val;
}

void image_cache_clear() {
    for (auto &pair : s_cache) {
        if (pair.second.img.pixels) {
            free(pair.second.img.pixels);
            pair.second.img.pixels = nullptr;
        }
    }
    s_cache.clear();
    s_redraw_needed = false;
}
