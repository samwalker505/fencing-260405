#pragma once

#include "app_input.h"

void app_hw_init(void);
void app_hw_poll_input(app_input_t *out, int *prev_top, int *prev_bottom,
                       int *prev_fencing);
