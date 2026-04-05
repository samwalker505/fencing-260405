/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "display_setup.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>

#define LED_PIN 1
#define BUTTON_PIN 3

void app_main(void) {
  display_setup();

  int pressed_count = 0;
  int last_btn_state = 0;

  gpio_config_t led_conf = {.pin_bit_mask = (1ULL << LED_PIN),
                            .mode = GPIO_MODE_OUTPUT,
                            .pull_up_en = GPIO_PULLUP_DISABLE,
                            .pull_down_en = GPIO_PULLDOWN_DISABLE,
                            .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&led_conf);

  // 2. Configure the Input Pin (Button)
  gpio_config_t btn_conf = {
      .pin_bit_mask = (1ULL << BUTTON_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE, // Internal pull-up for button to GND
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&btn_conf);

  while (1) {
    int btn_state = gpio_get_level(BUTTON_PIN);
    gpio_set_level(LED_PIN, btn_state);

    if (btn_state == 1 && last_btn_state == 0) {
      pressed_count++;
      printf("Pressed count: %d\n", pressed_count);
    }
    last_btn_state = btn_state;

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
