#ifndef SKEETS_COMPOSE_VIEW_H
#define SKEETS_COMPOSE_VIEW_H

#include "../app.h"
#include "../ui/input.h"

/* Draw the compose/reply view. */
void compose_view_draw(app_state_t *state);

/* Handle input for the compose view. */
void compose_view_handle(app_state_t *state, const input_event_t *ev);

/* Submit the composed post/reply. Returns 0 on success. */
int compose_view_submit(app_state_t *state, const char *text);

#endif /* SKEETS_COMPOSE_VIEW_H */
