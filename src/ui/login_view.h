#ifndef SKEETS_LOGIN_VIEW_H
#define SKEETS_LOGIN_VIEW_H

#include "../app.h"
#include "../ui/input.h"

/* Pre-populate login fields (e.g. from config.ini pre-fill). */
void login_view_prefill(const char *handle, const char *password, const char *pds_url);

/* Draw the login screen from scratch. */
void login_view_draw(app_state_t *state);

/* Handle a single input event while login is active. */
void login_view_handle(app_state_t *state, const input_event_t *ev);

#endif /* SKEETS_LOGIN_VIEW_H */
