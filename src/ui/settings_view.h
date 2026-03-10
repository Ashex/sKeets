#ifndef SKEETS_SETTINGS_VIEW_H
#define SKEETS_SETTINGS_VIEW_H

#include "../app.h"
#include "../ui/input.h"

/* Draw the settings view. */
void settings_view_draw(app_state_t *state);

/* Handle input for the settings view. */
void settings_view_handle(app_state_t *state, const input_event_t *ev);

/* Persist current settings to config file. */
void settings_save(const app_state_t *state);

#endif /* SKEETS_SETTINGS_VIEW_H */
