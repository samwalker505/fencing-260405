/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "display_setup.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

/* LilyGO T-Display S3 onboard buttons — pull-up, pressed = 0 */
#define BUTTON_TOP_GPIO GPIO_NUM_0     /* START / restart */
#define BUTTON_BOTTOM_GPIO GPIO_NUM_14 /* pause / resume */

#define FENCING_POINT_GPIO GPIO_NUM_3
#define LED_GPIO GPIO_NUM_1

/** Number of hits to record; average uses (TARGET_HITS - 1) inter-hit intervals. */
#define TARGET_HITS 10

typedef enum {
  STATE_RUNNING,
  STATE_PAUSED,
  STATE_DONE,
} app_state_t;

static void configure_onboard_button(gpio_num_t pin) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << pin,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
}

static void reset_session(int64_t *last_hit_ts, uint32_t *sum_inter_hit_us,
                          int *n_hits) {
  *last_hit_ts = 0;
  *sum_inter_hit_us = 0;
  *n_hits = 0;
}

void app_main(void) {
  display_setup();
  display_set_main_message("Top: START");

  int last_top = 1;
  int last_bottom = 1;
  int last_fp = 0;

  app_state_t state = STATE_DONE;

  int64_t last_hit_ts = 0;
  uint32_t sum_inter_hit_us = 0;
  int n_hits = 0;

  configure_onboard_button(BUTTON_TOP_GPIO);
  configure_onboard_button(BUTTON_BOTTOM_GPIO);

  gpio_config_t led_conf = {.pin_bit_mask = (1ULL << LED_GPIO),
                            .mode = GPIO_MODE_OUTPUT,
                            .pull_up_en = GPIO_PULLUP_DISABLE,
                            .pull_down_en = GPIO_PULLDOWN_DISABLE,
                            .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&led_conf);

  gpio_config_t fp_conf = {
      .pin_bit_mask = 1ULL << FENCING_POINT_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&fp_conf);

  char msg[96];
  const int intervals_needed = TARGET_HITS - 1;

  while (1) {
    int top = gpio_get_level(BUTTON_TOP_GPIO);
    int bottom = gpio_get_level(BUTTON_BOTTOM_GPIO);
    int fp = gpio_get_level(FENCING_POINT_GPIO);

    display_set_hw_buttons_visual(top == 0, bottom == 0);

    /* LED follows fencing input (high = on) */
    gpio_set_level(LED_GPIO, fp);

    bool top_press = (top == 0 && last_top == 1);
    bool bottom_press = (bottom == 0 && last_bottom == 1);
    bool hit_rising = (fp == 1 && last_fp == 0);

    if (top_press) {
      reset_session(&last_hit_ts, &sum_inter_hit_us, &n_hits);
      state = STATE_RUNNING;
      snprintf(msg, sizeof msg, "Hits 0/%d", TARGET_HITS);
      display_set_main_message(msg);
      printf("start\n");
    }

    if (bottom_press) {
      if (state == STATE_RUNNING) {
        state = STATE_PAUSED;
        display_set_main_message("PAUSED");
        printf("pause\n");
      } else if (state == STATE_PAUSED) {
        state = STATE_RUNNING;
        snprintf(msg, sizeof msg, "Hits %d/%d", n_hits, TARGET_HITS);
        display_set_main_message(msg);
        printf("resume\n");
      }
    }

    if (state == STATE_RUNNING && hit_rising) {
      int64_t now = esp_timer_get_time();

      if (n_hits == 0) {
        last_hit_ts = now;
        n_hits = 1;
        snprintf(msg, sizeof msg, "Hits %d/%d", n_hits, TARGET_HITS);
        display_set_main_message(msg);
      } else {
        sum_inter_hit_us += (uint32_t)(now - last_hit_ts);
        last_hit_ts = now;
        n_hits++;

        if (n_hits < TARGET_HITS) {
          snprintf(msg, sizeof msg, "Hits %d/%d", n_hits, TARGET_HITS);
          display_set_main_message(msg);
        } else {
          double avg_between_ms =
              (sum_inter_hit_us / (double)intervals_needed) / 1000.0;
          snprintf(msg, sizeof msg, "Done\nAvg between hits:\n%.1f ms",
                   avg_between_ms);
          display_set_main_message(msg);
          printf("avg_between_hits_ms=%.2f (hits=%d intervals=%d)\n",
                 avg_between_ms, TARGET_HITS, intervals_needed);
          state = STATE_DONE;
        }
      }
    }

    last_top = top;
    last_bottom = bottom;
    last_fp = fp;

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
