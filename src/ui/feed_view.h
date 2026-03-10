#ifndef SKEETS_FEED_VIEW_H
#define SKEETS_FEED_VIEW_H

#include "../app.h"
#include "../ui/input.h"

/* Draw the feed timeline. */
void feed_view_draw(app_state_t *state);

/* Handle input for the feed view. */
void feed_view_handle(app_state_t *state, const input_event_t *ev);

/* Reload the timeline (fetches fresh data). */
void feed_view_refresh(app_state_t *state);

#endif /* SKEETS_FEED_VIEW_H */
