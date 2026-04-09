/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "app_hw.h"
#include "app_state.h"
#include "display_setup.h"
#include "screen_menu.h"
#include "screen_speed_test.h"
#include "screen_training.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void app_transition(app_context_t *ctx, app_screen_t to,
                           const app_input_t *in) {
  ctx->screen = to;
  switch (to) {
  case APP_SCREEN_MENU:
    screen_menu_on_enter(ctx);
    break;
  case APP_SCREEN_SPEED_TEST:
    screen_speed_test_on_enter(ctx);
    break;
  case APP_SCREEN_TRAINING:
    screen_training_on_enter(ctx, in);
    break;
  }
}

void app_main(void) {
  display_setup();

  app_context_t ctx = {.screen = APP_SCREEN_MENU,
                       .menu_selected = 0,
                       .speed_phase = APP_SPEED_DONE,
                       .last_hit_us = 0,
                       .sum_inter_hit_us = 0,
                       .n_hits = 0};

  int prev_top = 1;
  int prev_bottom = 1;
  int prev_fp = 0;

  app_hw_init();
  app_transition(&ctx, APP_SCREEN_MENU, NULL);

  app_input_t in;

  while (1) {
    app_hw_poll_input(&in, &prev_top, &prev_bottom, &prev_fp);

    app_screen_t next = ctx.screen;
    switch (ctx.screen) {
    case APP_SCREEN_MENU:
      next = screen_menu_update(&ctx, &in);
      break;
    case APP_SCREEN_SPEED_TEST:
      next = screen_speed_test_update(&ctx, &in);
      break;
    case APP_SCREEN_TRAINING:
      next = screen_training_update(&ctx, &in);
      break;
    }

    if (next != ctx.screen) {
      app_transition(&ctx, next, &in);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
