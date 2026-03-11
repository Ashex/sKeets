#include "input.h"
#include "fb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <time.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <linux/input.h>

/* Save Linux key codes we need, then undef macros that collide with kobo_key_t. */
static constexpr int LINUX_KEY_POWER = KEY_POWER;
#undef KEY_POWER
#undef KEY_FORWARD
#undef KEY_BACK
#undef KEY_HOME

#define MAX_INPUT_DEVICES 8
#define TAP_MAX_DIST      30   /* pixels */
#define TAP_MAX_MS        300  /* milliseconds */
#define LONG_PRESS_MS     500  /* milliseconds */

struct input_device {
    int fd;
    int x_code;
    int y_code;
    int x_min;
    int x_max;
    int y_min;
    int y_max;
    bool has_touch_axes;
    bool swap_axes;
};

struct input_ctx {
    input_device devices[MAX_INPUT_DEVICES];
    int       nfds;
    int       fb_w;
    int       fb_h;
    /* MT tracking */
    int       cur_x;
    int       cur_y;
    bool      touching;
    long long touch_down_ms;
    int       touch_down_x;
    int       touch_down_y;
    touch_type_t last_emit_type;
};

/* Millisecond timestamp */
static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static bool query_abs_axis(int fd, int primary_code, int fallback_code,
                           int *resolved_code, int *min_value, int *max_value) {
    struct input_absinfo absinfo = {};
    if (ioctl(fd, EVIOCGABS(primary_code), &absinfo) == 0) {
        if (resolved_code) *resolved_code = primary_code;
        if (min_value) *min_value = absinfo.minimum;
        if (max_value) *max_value = absinfo.maximum;
        return true;
    }

    if (ioctl(fd, EVIOCGABS(fallback_code), &absinfo) == 0) {
        if (resolved_code) *resolved_code = fallback_code;
        if (min_value) *min_value = absinfo.minimum;
        if (max_value) *max_value = absinfo.maximum;
        return true;
    }

    return false;
}

static bool should_swap_axes(int x_min, int x_max, int y_min, int y_max, int fb_w, int fb_h) {
    const int x_span = x_max - x_min;
    const int y_span = y_max - y_min;
    const int normal_score = abs(x_span - (fb_w - 1)) + abs(y_span - (fb_h - 1));
    const int swapped_score = abs(x_span - (fb_h - 1)) + abs(y_span - (fb_w - 1));
    return swapped_score < normal_score;
}

static int normalize_axis(int value, int min_value, int max_value, int output_max) {
    if (output_max <= 0) return 0;
    if (max_value <= min_value) {
        if (value < 0) return 0;
        if (value > output_max) return output_max;
        return value;
    }

    if (value < min_value) value = min_value;
    if (value > max_value) value = max_value;

    const long long numerator = (long long)(value - min_value) * (long long)output_max;
    const long long denominator = (long long)(max_value - min_value);
    return (int)(numerator / denominator);
}

static void map_touch_point(const input_ctx_t *ctx, const input_device &device,
                            int raw_x, int raw_y, int *mapped_x, int *mapped_y) {
    int x = normalize_axis(raw_x, device.x_min, device.x_max,
                           device.swap_axes ? ctx->fb_h - 1 : ctx->fb_w - 1);
    int y = normalize_axis(raw_y, device.y_min, device.y_max,
                           device.swap_axes ? ctx->fb_w - 1 : ctx->fb_h - 1);

    if (device.swap_axes) {
        int tmp = x;
        x = y;
        y = tmp;
    }

    if (mapped_x) *mapped_x = x;
    if (mapped_y) *mapped_y = y;
}

/* Find and open all /dev/input/event* files that look like touch/key devices */
input_ctx_t *input_open(int fb_w, int fb_h) {
    input_ctx_t *ctx = (input_ctx_t *)calloc(1, sizeof(input_ctx_t));
    if (!ctx) return NULL;
    ctx->fb_w = fb_w;
    ctx->fb_h = fb_h;
    ctx->last_emit_type = TOUCH_NONE;
    for (int i = 0; i < MAX_INPUT_DEVICES; i++)
        ctx->devices[i].fd = -1;

    DIR *d = opendir("/dev/input");
    if (!d) { free(ctx); return NULL; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && ctx->nfds < MAX_INPUT_DEVICES) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        input_device &device = ctx->devices[ctx->nfds];
        memset(&device, 0, sizeof(device));
        device.fd = fd;
        device.x_code = ABS_MT_POSITION_X;
        device.y_code = ABS_MT_POSITION_Y;
        device.has_touch_axes = query_abs_axis(fd, ABS_MT_POSITION_X, ABS_X,
                                               &device.x_code, &device.x_min, &device.x_max) &&
                                query_abs_axis(fd, ABS_MT_POSITION_Y, ABS_Y,
                                               &device.y_code, &device.y_min, &device.y_max);
        if (device.has_touch_axes) {
            device.swap_axes = should_swap_axes(device.x_min, device.x_max,
                                                device.y_min, device.y_max,
                                                ctx->fb_w, ctx->fb_h);
            fprintf(stderr,
                    "input_open: %s touch axes x=%d[%d..%d] y=%d[%d..%d] swap=%d\n",
                    path,
                    device.x_code, device.x_min, device.x_max,
                    device.y_code, device.y_min, device.y_max,
                    device.swap_axes ? 1 : 0);
        }

        if (ioctl(fd, EVIOCGRAB, 1) == 0)
            fprintf(stderr, "input_open: grabbed %s\n", path);
        else
            fprintf(stderr, "input_open: warning: could not grab %s\n", path);
        ctx->nfds++;
    }
    closedir(d);
    return ctx;
}

void input_close(input_ctx_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->nfds; i++)
        if (ctx->devices[i].fd >= 0) {
            ioctl(ctx->devices[i].fd, EVIOCGRAB, 0);
            close(ctx->devices[i].fd);
        }
    free(ctx);
}

bool input_poll(input_ctx_t *ctx, input_event_t *ev, int timeout_ms) {
    if (!ctx || !ev || ctx->nfds == 0) return false;
    ev->type = INPUT_NONE;

    struct pollfd pfds[MAX_INPUT_DEVICES];
    for (int i = 0; i < ctx->nfds; i++) {
        pfds[i].fd     = ctx->devices[i].fd;
        pfds[i].events = POLLIN;
    }

    int ret = poll(pfds, (nfds_t)ctx->nfds, timeout_ms);
    if (ret <= 0) return false;

    for (int i = 0; i < ctx->nfds; i++) {
        if (!(pfds[i].revents & POLLIN)) continue;

        input_device &device = ctx->devices[i];

        struct input_event ie;
        while (read(device.fd, &ie, sizeof(ie)) == sizeof(ie)) {
            switch (ie.type) {
                case EV_ABS:
                    if (device.has_touch_axes) {
                        if (ie.code == device.x_code) {
                            ctx->cur_x = ie.value;
                        } else if (ie.code == device.y_code) {
                            ctx->cur_y = ie.value;
                        }
                    }
                    break;

                case EV_KEY:
                    switch (ie.code) {
                        case BTN_TOUCH:
                            if (ie.value == 1) {
                                /* Touch down */
                                int mapped_x = ctx->cur_x;
                                int mapped_y = ctx->cur_y;
                                if (device.has_touch_axes)
                                    map_touch_point(ctx, device, ctx->cur_x, ctx->cur_y, &mapped_x, &mapped_y);
                                ctx->touching      = true;
                                ctx->touch_down_ms = now_ms();
                                ctx->touch_down_x  = mapped_x;
                                ctx->touch_down_y  = mapped_y;
                                ctx->last_emit_type = TOUCH_DOWN;
                                ev->type       = INPUT_TOUCH;
                                ev->touch.type = TOUCH_DOWN;
                                ev->touch.x    = mapped_x;
                                ev->touch.y    = mapped_y;
                                return true;
                            } else {
                                /* Touch up */
                                int mapped_x = ctx->cur_x;
                                int mapped_y = ctx->cur_y;
                                if (device.has_touch_axes)
                                    map_touch_point(ctx, device, ctx->cur_x, ctx->cur_y, &mapped_x, &mapped_y);
                                ctx->touching = false;
                                ctx->last_emit_type = TOUCH_NONE;
                                ev->type       = INPUT_TOUCH;
                                ev->touch.type = TOUCH_UP;
                                ev->touch.x    = mapped_x;
                                ev->touch.y    = mapped_y;
                                return true;
                            }
                        case LINUX_KEY_POWER:
                            if (ie.value == 1) {
                                ev->type = INPUT_KEY;
                                ev->key  = KEY_POWER;
                                return true;
                            }
                            break;
                        case KEY_NEXTSONG:
                        case 104: /* Kobo forward page */
                            if (ie.value == 1) {
                                ev->type = INPUT_KEY;
                                ev->key  = KEY_FORWARD;
                                return true;
                            }
                            break;
                        case KEY_PREVIOUSSONG:
                        case 109: /* Kobo back page */
                            if (ie.value == 1) {
                                ev->type = INPUT_KEY;
                                ev->key  = KEY_BACK;
                                return true;
                            }
                            break;
                    }
                    break;

                case EV_SYN:
                    if (ctx->touching) {
                        int mapped_x = ctx->cur_x;
                        int mapped_y = ctx->cur_y;
                        if (device.has_touch_axes)
                            map_touch_point(ctx, device, ctx->cur_x, ctx->cur_y, &mapped_x, &mapped_y);

                        /* Long-press detection */
                        long long elapsed = now_ms() - ctx->touch_down_ms;
                        int dx = mapped_x - ctx->touch_down_x;
                        int dy = mapped_y - ctx->touch_down_y;
                        int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                        if (elapsed > LONG_PRESS_MS && dist <= TAP_MAX_DIST &&
                            ctx->last_emit_type != TOUCH_LONG_PRESS) {
                            ctx->last_emit_type = TOUCH_LONG_PRESS;
                            ev->type       = INPUT_TOUCH;
                            ev->touch.type = TOUCH_LONG_PRESS;
                            ev->touch.x    = mapped_x;
                            ev->touch.y    = mapped_y;
                            return true;
                        }

                        ctx->last_emit_type = TOUCH_MOVE;
                        ev->type       = INPUT_TOUCH;
                        ev->touch.type = TOUCH_MOVE;
                        ev->touch.x    = mapped_x;
                        ev->touch.y    = mapped_y;
                        return true;
                    }
                    break;
            }
        }
    }
    return false;
}

bool input_is_tap(const touch_event_t *down, const touch_event_t *up) {
    if (!down || !up) return false;
    int dx = down->x - up->x;
    int dy = down->y - up->y;
    int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    return dist <= TAP_MAX_DIST;
}
