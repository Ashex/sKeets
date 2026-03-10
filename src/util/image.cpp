#include "image.h"
#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

/* ── public API ───────────────────────────────────────────────────── */

void image_cache_path(const char *url, char *out_path, int out_size) {
    unsigned long h = str_hash(url);
    snprintf(out_path, out_size, "%s%lx.img", IMAGE_CACHE_DIR, h);
}

int image_load_file(const char *path, image_t *out) {
    int channels;
    out->pixels = stbi_load(path, &out->width, &out->height, &channels, 4);
    if (!out->pixels) return -1;
    return 0;
}

int image_load_url(const char *url, image_t *out) {
    /* Check cache first */
    char cache_path[512];
    image_cache_path(url, cache_path, sizeof(cache_path));

    struct stat st;
    if (stat(cache_path, &st) == 0 && st.st_size > 0) {
        return image_load_file(cache_path, out);
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
    (void)mkdir(IMAGE_CACHE_DIR, 0755);
    FILE *f = fopen(cache_path, "wb");
    if (f) {
        fwrite(&out->width,  sizeof(int), 1, f);
        fwrite(&out->height, sizeof(int), 1, f);
        fwrite(out->pixels, 1, (size_t)(out->width * out->height * 4), f);
        fclose(f);
    }

    return 0;
}

int image_scale_to_fit(image_t *img, int max_w, int max_h) {
    if (!img || !img->pixels) return -1;
    if (img->width <= max_w && img->height <= max_h) return 0;

    float sx = (float)max_w / (float)img->width;
    float sy = (float)max_h / (float)img->height;
    float s  = sx < sy ? sx : sy;

    int new_w = (int)(img->width  * s);
    int new_h = (int)(img->height * s);
    if (new_w < 1) new_w = 1;
    if (new_h < 1) new_h = 1;

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

void image_to_rgb565(const image_t *img, uint16_t *dst) {
    if (!img || !img->pixels || !dst) return;
    int n = img->width * img->height;
    const uint8_t *src = img->pixels;
    for (int i = 0; i < n; i++, src += 4) {
        uint16_t r = (src[0] >> 3) & 0x1F;
        uint16_t g = (src[1] >> 2) & 0x3F;
        uint16_t b = (src[2] >> 3) & 0x1F;
        dst[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

void image_free(image_t *img) {
    if (!img) return;
    if (img->pixels) { stbi_image_free(img->pixels); img->pixels = NULL; }
    img->width = img->height = 0;
}
