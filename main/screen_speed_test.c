#include "screen_speed_test.h"

#include "app_config.h"
#include "display_setup.h"
#include "esp_timer.h"
#include <stdio.h>

static void session_reset(app_context_t *ctx) {
  ctx->last_hit_us = 0;
  ctx->sum_inter_hit_us = 0;
  ctx->n_hits = 0;
}

static void session_start_fresh(app_context_t *ctx) {
  session_reset(ctx);
  ctx->speed_phase = APP_SPEED_RUNNING;
  display_set_title("");
  display_speed_test_ui_begin(APP_SPEED_TARGET_HITS);
  printf("speed test start\n");
}

void screen_speed_test_on_enter(app_context_t *ctx) {
  display_restore_standard_look();
  display_set_menu_look(false);
  display_set_title_visible(false);
  display_set_button_chrome(true, "Speed Test Mode", "next");
  session_start_fresh(ctx);
}

app_screen_t screen_speed_test_update(app_context_t *ctx, const app_input_t *in) {
  const int intervals_needed = APP_SPEED_TARGET_HITS - 1;

  if (in->go_press) {
    display_speed_test_ui_set_paused(false);
    session_start_fresh(ctx);
    return APP_SCREEN_SPEED_TEST;
  }

  if (in->next_press) {
    if (ctx->speed_phase == APP_SPEED_DONE) {
      return APP_SCREEN_MENU;
    }
    if (ctx->speed_phase == APP_SPEED_RUNNING) {
      ctx->speed_phase = APP_SPEED_PAUSED;
      display_speed_test_ui_set_paused(true);
      printf("pause\n");
    } else if (ctx->speed_phase == APP_SPEED_PAUSED) {
      ctx->speed_phase = APP_SPEED_RUNNING;
      display_speed_test_ui_set_paused(false);
      printf("resume\n");
    }
    return APP_SCREEN_SPEED_TEST;
  }

  if (ctx->speed_phase != APP_SPEED_RUNNING || !in->fencing_rising) {
    return APP_SCREEN_SPEED_TEST;
  }

  int64_t now = esp_timer_get_time();

  if (ctx->n_hits == 0) {
    ctx->last_hit_us = now;
    ctx->n_hits = 1;
    display_speed_test_ui_set_progress(ctx->n_hits, APP_SPEED_TARGET_HITS);
    return APP_SCREEN_SPEED_TEST;
  }

  ctx->sum_inter_hit_us += (uint32_t)(now - ctx->last_hit_us);
  ctx->last_hit_us = now;
  ctx->n_hits++;

  if (ctx->n_hits < APP_SPEED_TARGET_HITS) {
    display_speed_test_ui_set_progress(ctx->n_hits, APP_SPEED_TARGET_HITS);
    return APP_SCREEN_SPEED_TEST;
  }

  double avg_between_ms =
      (ctx->sum_inter_hit_us / (double)intervals_needed) / 1000.0;
  if (ctx->speed_phase == APP_SPEED_PAUSED) {
    display_speed_test_ui_set_paused(false);
  }
  display_speed_test_ui_show_average_ms(avg_between_ms);
  printf("avg_between_hits_ms=%.2f\n", avg_between_ms);
  ctx->speed_phase = APP_SPEED_DONE;

  return APP_SCREEN_SPEED_TEST;
}
