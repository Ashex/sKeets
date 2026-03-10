#ifndef SKEETS_VIEWS_H
#define SKEETS_VIEWS_H

/* Shared view enum referenced by both app.h and individual view headers */

typedef enum {
    VIEW_LOGIN,
    VIEW_FEED,
    VIEW_THREAD,
    VIEW_COMPOSE,
    VIEW_SETTINGS,
} app_view_t;

/* ── Common layout constants ──────────────────────────────────────── */

#define UI_MARGIN        16   /* outer horizontal margin */
#define UI_PAD           8    /* inner padding */
#define UI_BAR_H         52   /* top/bottom bar height */
#define UI_MIN_TAP       44   /* minimum touch target size */

/* Number of posts to request per timeline page */
#define FEED_PAGE_SIZE   50

/* ── Boundary-condition helpers ──────────────────────────────────── */

/* Clamp a scroll offset into [0, max_scroll]. */
static inline int clamp_scroll(int scroll, int max_scroll) {
    if (max_scroll < 0) max_scroll = 0;
    if (scroll < 0)          return 0;
    if (scroll > max_scroll) return max_scroll;
    return scroll;
}

/* Returns true when the point (px, py) falls inside the rectangle
 * defined by its top-left corner (rx, ry) and dimensions (rw, rh). */
static inline int hit_test_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

/* ── Forward declarations used across views ─────────────────────── */

typedef struct app_state app_state_t;  /* defined in app.h */

#endif /* SKEETS_VIEWS_H */
