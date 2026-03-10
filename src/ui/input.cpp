#include "input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <time.h>
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

struct input_ctx {
    int       fds[MAX_INPUT_DEVICES];
    int       nfds;
    /* MT tracking */
    int       cur_x;
    int       cur_y;
    bool      touching;
    long long touch_down_ms;
    int       touch_down_x;
    int       touch_down_y;
};

/* Millisecond timestamp */
static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* Find and open all /dev/input/event* files that look like touch/key devices */
input_ctx_t *input_open(void) {
    input_ctx_t *ctx = (input_ctx_t *)calloc(1, sizeof(input_ctx_t));
    if (!ctx) return NULL;
    for (int i = 0; i < MAX_INPUT_DEVICES; i++)
        ctx->fds[i] = -1;

    DIR *d = opendir("/dev/input");
    if (!d) { free(ctx); return NULL; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && ctx->nfds < MAX_INPUT_DEVICES) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        ctx->fds[ctx->nfds++] = fd;
    }
    closedir(d);
    return ctx;
}

void input_close(input_ctx_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->nfds; i++)
        if (ctx->fds[i] >= 0) close(ctx->fds[i]);
    free(ctx);
}

bool input_poll(input_ctx_t *ctx, input_event_t *ev, int timeout_ms) {
    if (!ctx || !ev || ctx->nfds == 0) return false;
    ev->type = INPUT_NONE;

    struct pollfd pfds[MAX_INPUT_DEVICES];
    for (int i = 0; i < ctx->nfds; i++) {
        pfds[i].fd     = ctx->fds[i];
        pfds[i].events = POLLIN;
    }

    int ret = poll(pfds, (nfds_t)ctx->nfds, timeout_ms);
    if (ret <= 0) return false;

    for (int i = 0; i < ctx->nfds; i++) {
        if (!(pfds[i].revents & POLLIN)) continue;

        struct input_event ie;
        while (read(ctx->fds[i], &ie, sizeof(ie)) == sizeof(ie)) {
            switch (ie.type) {
                case EV_ABS:
                    switch (ie.code) {
                        case ABS_MT_POSITION_X:
                        case ABS_X:
                            ctx->cur_x = ie.value;
                            break;
                        case ABS_MT_POSITION_Y:
                        case ABS_Y:
                            ctx->cur_y = ie.value;
                            break;
                    }
                    break;

                case EV_KEY:
                    switch (ie.code) {
                        case BTN_TOUCH:
                            if (ie.value == 1) {
                                /* Touch down */
                                ctx->touching      = true;
                                ctx->touch_down_ms = now_ms();
                                ctx->touch_down_x  = ctx->cur_x;
                                ctx->touch_down_y  = ctx->cur_y;
                                ev->type       = INPUT_TOUCH;
                                ev->touch.type = TOUCH_DOWN;
                                ev->touch.x    = ctx->cur_x;
                                ev->touch.y    = ctx->cur_y;
                                return true;
                            } else {
                                /* Touch up */
                                ctx->touching = false;
                                ev->type       = INPUT_TOUCH;
                                ev->touch.type = TOUCH_UP;
                                ev->touch.x    = ctx->cur_x;
                                ev->touch.y    = ctx->cur_y;
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
                        ev->type       = INPUT_TOUCH;
                        ev->touch.type = TOUCH_MOVE;
                        ev->touch.x    = ctx->cur_x;
                        ev->touch.y    = ctx->cur_y;
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
