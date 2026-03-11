#pragma once
#include "views.h"
#include "../app.h"
#include "../ui/input.h"

/* Draw the auth waiting screen (instructions splash). */
void auth_view_draw(app_state_t *state);

/* Handle input events on the auth screen. */
void auth_view_handle(app_state_t *state, const input_event_t *ev);

/* Set an error message to display on the auth screen. Pass NULL to clear. */
void auth_view_set_error(const char *msg);

/* Set an informational status message on the auth screen. Pass NULL to clear. */
void auth_view_set_info(const char *msg);
