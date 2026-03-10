#ifndef SKEETS_THREAD_VIEW_H
#define SKEETS_THREAD_VIEW_H

#include "../app.h"
#include "../ui/input.h"

/* Draw the thread/comments view.
 * state->selected_post must be set before calling. */
void thread_view_draw(app_state_t *state);

/* Handle input for the thread view. */
void thread_view_handle(app_state_t *state, const input_event_t *ev);

/* Load thread data for state->selected_post->uri. */
void thread_view_load(app_state_t *state);

#endif /* SKEETS_THREAD_VIEW_H */
