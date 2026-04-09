#pragma once

#include "app_input.h"
#include "app_state.h"

/** Call when `ctx->screen` becomes APP_SCREEN_MENU. */
void screen_menu_on_enter(app_context_t *ctx);

/**
 * Handle one poll while on the menu screen.
 * @return screen to use next (usually unchanged; SPEED_TEST when user opens a mode).
 */
app_screen_t screen_menu_update(app_context_t *ctx, const app_input_t *in);
