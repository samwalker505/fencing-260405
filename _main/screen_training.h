#pragma once

#include "app_input.h"
#include "app_state.h"

/** Call when entering training; uses `in` for initial fencing line state (no flash). */
void screen_training_on_enter(app_context_t *ctx, const app_input_t *in);

app_screen_t screen_training_update(app_context_t *ctx, const app_input_t *in);
