#include "image.h"
#include "paths.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

/* Pull in stb_image */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

/* Pull in stb_image_resize */
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QUrl>
#include <QByteArray>

/* ── Disk cache helpers ────────────────────────────────────────────── */

#define CACHE_MAGIC 0x534B4943  /* "SKIC" */

/* Read a raw cached image (magic + w + h + RGBA pixels). */
static int image_load_cached(const char *path, image_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic = 0;
    int w = 0, h = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != CACHE_MAGIC) {
        fclose(f);
        /* Not our raw format — try stbi (handles legacy or real image files). */
        int channels;
        out->pixels = stbi_load(path, &out->width, &out->height, &channels, 4);
        return out->pixels ? 0 : -1;
    }
    if (fread(&w, sizeof(int), 1, f) != 1 ||
        fread(&h, sizeof(int), 1, f) != 1 || w <= 0 || h <= 0) {
        fclose(f); return -1;
    }

    size_t sz = (size_t)w * (size_t)h * 4;
    uint8_t *px = (uint8_t *)malloc(sz);
    if (!px) { fclose(f); return -1; }
    if (fread(px, 1, sz, f) != sz) { free(px); fclose(f); return -1; }
    fclose(f);

    out->pixels = px;
    out->width  = w;
    out->height = h;
    return 0;
}

/* Write a raw cached image with magic header. */
static void image_write_cache(const char *path, const image_t *img) {
    (void)skeets_ensure_data_dirs();
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t magic = CACHE_MAGIC;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&img->width,  sizeof(int), 1, f);
    fwrite(&img->height, sizeof(int), 1, f);
    fwrite(img->pixels, 1, (size_t)(img->width * img->height * 4), f);
    fclose(f);
}

/* ── public API ───────────────────────────────────────────────────── */

static const char *image_cache_prefix(image_cache_kind_t kind) {
    return kind == IMAGE_CACHE_KIND_AVATAR ? "avatar" : "embed";
}

void image_cache_path_with_kind(const char *url, image_cache_kind_t kind, char *out_path, int out_size) {
    unsigned long h = str_hash(url);
    snprintf(out_path, out_size, "%s/%s-%lx.img", skeets_cache_dir(), image_cache_prefix(kind), h);
}

void image_cache_path(const char *url, char *out_path, int out_size) {
    image_cache_path_with_kind(url, IMAGE_CACHE_KIND_EMBED, out_path, out_size);
}

int image_load_file(const char *path, image_t *out) {
    return image_load_cached(path, out);
}

int image_load_url(const char *url, image_t *out) {
    return image_load_url_with_kind(url, IMAGE_CACHE_KIND_EMBED, out);
}

int image_load_url_with_kind(const char *url, image_cache_kind_t kind, image_t *out) {
    /* Check cache first */
    char cache_path[512];
    image_cache_path_with_kind(url, kind, cache_path, sizeof(cache_path));

    struct stat st;
    if (stat(cache_path, &st) == 0 && st.st_size > 0) {
        return image_load_cached(cache_path, out);
    }

    /* Download using Qt Network */
    QNetworkAccessManager nam;
    QNetworkRequest request(QUrl(QString::fromUtf8(url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QEventLoop loop;
    QNetworkReply *reply = nam.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return -1;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.isEmpty()) return -1;

    /* Decode from memory */
    int channels;
    out->pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(data.constData()),
        data.size(), &out->width, &out->height, &channels, 4);
    if (!out->pixels) return -1;

    /* Write to cache */
    image_write_cache(cache_path, out);

    return 0;
}

int image_scale_to_fit(image_t *img, int max_w, int max_h) {
    if (!img || !img->pixels) return -1;
    if (max_w <= 0 || max_h <= 0) return 0;

    float sx = (float)max_w / (float)img->width;
    float sy = (float)max_h / (float)img->height;
    float s  = sx < sy ? sx : sy;

    int new_w = (int)(img->width  * s);
    int new_h = (int)(img->height * s);
    if (new_w < 1) new_w = 1;
    if (new_h < 1) new_h = 1;
    if (new_w == img->width && new_h == img->height) return 0;

    uint8_t *dst = (uint8_t *)malloc((size_t)(new_w * new_h * 4));
    if (!dst) return -1;

    stbir_resize_uint8(img->pixels, img->width, img->height, 0,
                       dst, new_w, new_h, 0, 4);

    free(img->pixels);
    img->pixels = dst;
    img->width  = new_w;
    img->height = new_h;
    return 0;
}

int image_scaled_copy(const image_t *src, int target_w, int target_h, image_t *out) {
    if (!src || !src->pixels || !out || target_w <= 0 || target_h <= 0) return -1;

    out->pixels = NULL;
    out->width = 0;
    out->height = 0;

    if (src->width == target_w && src->height == target_h) {
        size_t sz = (size_t)target_w * (size_t)target_h * 4;
        uint8_t *dst = (uint8_t *)malloc(sz);
        if (!dst) return -1;
        memcpy(dst, src->pixels, sz);
        out->pixels = dst;
        out->width = target_w;
        out->height = target_h;
        return 0;
    }

    uint8_t *dst = (uint8_t *)malloc((size_t)target_w * (size_t)target_h * 4);
    if (!dst) return -1;

    stbir_resize_uint8(src->pixels, src->width, src->height, 0,
                       dst, target_w, target_h, 0, 4);

    out->pixels = dst;
    out->width = target_w;
    out->height = target_h;
    return 0;
}

void image_free(image_t *img) {
    if (!img) return;
    if (img->pixels) { free(img->pixels); img->pixels = NULL; }
    img->width = img->height = 0;
}

int image_decode_memory(const uint8_t *data, int len, image_t *out) {
    if (!data || len <= 0 || !out) return -1;
    int channels;
    out->pixels = stbi_load_from_memory(data, len, &out->width, &out->height, &channels, 4);
    return out->pixels ? 0 : -1;
}

void image_write_disk_cache(const char *url, const image_t *img) {
    image_write_disk_cache_with_kind(url, IMAGE_CACHE_KIND_EMBED, img);
}

void image_write_disk_cache_with_kind(const char *url, image_cache_kind_t kind, const image_t *img) {
    if (!url || !img || !img->pixels) return;
    char path[512];
    image_cache_path_with_kind(url, kind, path, sizeof(path));
    image_write_cache(path, img);
}

void image_clear_disk_cache(bool include_embeds, bool include_avatars) {
    const char *cache_dir = skeets_cache_dir();
    DIR *d = opendir(cache_dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        const bool is_avatar = strncmp(ent->d_name, "avatar-", 7) == 0;
        const bool is_embed = strncmp(ent->d_name, "embed-", 6) == 0 || strncmp(ent->d_name, "v2-", 3) == 0;
        if ((is_avatar && !include_avatars) || (is_embed && !include_embeds) || (!is_avatar && !is_embed)) {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", cache_dir, ent->d_name);
        remove(path);
    }

    closedir(d);
}

void image_evict_disk_cache(size_t max_bytes) {
    const char *cache_dir = skeets_cache_dir();
    DIR *d = opendir(cache_dir);
    if (!d) return;

    struct cache_file_info {
        char   path[512];
        size_t size;
        time_t mtime;
    };

    /* Heap-allocate to avoid stack overflow on embedded targets. */
#define MAX_CACHE_SCAN 2048
    struct cache_file_info *files =
        (struct cache_file_info *)malloc(MAX_CACHE_SCAN * sizeof(struct cache_file_info));
    if (!files) { closedir(d); return; }

    int    nfiles = 0;
    size_t total  = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && nfiles < MAX_CACHE_SCAN) {
        if (ent->d_name[0] == '.') continue;
        struct cache_file_info *fi = &files[nfiles];
        snprintf(fi->path, sizeof(fi->path), "%s/%s", cache_dir, ent->d_name);
        struct stat st;
        if (stat(fi->path, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        fi->size  = (size_t)st.st_size;
        fi->mtime = st.st_mtime;
        total    += fi->size;
        nfiles++;
    }
    closedir(d);

    if (total <= max_bytes) return;

    /* Bubble sort ascending by mtime (oldest first). */
    for (int i = 0; i < nfiles - 1; i++) {
        for (int j = i + 1; j < nfiles; j++) {
            if (files[j].mtime < files[i].mtime) {
                struct cache_file_info tmp = files[i];
                files[i] = files[j];
                files[j] = tmp;
            }
        }
    }

    for (int i = 0; i < nfiles && total > max_bytes; i++) {
        if (remove(files[i].path) == 0) {
            total -= files[i].size;
            fprintf(stderr, "image_evict_disk_cache: evicted %s\n", files[i].path);
        }
    }

    free(files);
#undef MAX_CACHE_SCAN
}
