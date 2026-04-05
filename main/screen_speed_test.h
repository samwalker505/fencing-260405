#pragma once

#include "app_input.h"
#include "app_state.h"

/** Call when entering the speed-test screen (from menu or after navigation). */
void screen_speed_test_on_enter(app_context_t *ctx);

/**
 * Handle one poll while on the speed-test screen.
 * @return APP_SCREEN_MENU when user backs out after a finished run; else SPEED_TEST.
 */
app_screen_t screen_speed_test_update(app_context_t *ctx, const app_input_t *in);
