#ifndef SKEETS_INPUT_H
#define SKEETS_INPUT_H

#include <stdbool.h>

/* ── Touch event ─────────────────────────────────────────────────── */

typedef enum {
    TOUCH_DOWN,
    TOUCH_UP,
    TOUCH_MOVE,
    TOUCH_NONE,
    TOUCH_SWIPE_LEFT,
    TOUCH_SWIPE_RIGHT,
    TOUCH_SWIPE_UP,
    TOUCH_SWIPE_DOWN,
    TOUCH_LONG_PRESS,
} touch_type_t;

typedef struct {
    touch_type_t type;
    int          x;
    int          y;
} touch_event_t;

/* ── Button / power events ───────────────────────────────────────── */

typedef enum {
    KEY_NONE    = 0,
    KEY_POWER   = 1,
    KEY_FORWARD = 2,
    KEY_BACK    = 3,
    KEY_HOME    = 4,
} kobo_key_t;

/* ── Combined input event ────────────────────────────────────────── */

typedef enum {
    INPUT_NONE,
    INPUT_TOUCH,
    INPUT_KEY,
} input_type_t;

typedef struct {
    input_type_t type;
    touch_event_t touch;
    kobo_key_t    key;
} input_event_t;

/* ── Input context ───────────────────────────────────────────────── */

typedef struct input_ctx input_ctx_t;

/*
 * Open /dev/input/event* devices.
 * fb_w and fb_h are the framebuffer dimensions used for axis mapping.
 * Returns a heap-allocated context, or NULL on failure.
 */
input_ctx_t *input_open(int fb_w, int fb_h);

/* Close all input fds and free context. */
void input_close(input_ctx_t *ctx);

/*
 * Poll for the next input event.
 * timeout_ms: milliseconds to wait; 0 = non-blocking.
 * Returns true if an event was read, false on timeout/error.
 */
bool input_poll(input_ctx_t *ctx, input_event_t *ev, int timeout_ms);

/* Returns true if the given touch event is a tap (not a drag). */
bool input_is_tap(const touch_event_t *down, const touch_event_t *up);

#endif /* SKEETS_INPUT_H */
