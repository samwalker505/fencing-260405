#include "demo.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>

#define LED_PIN 2
#define BUTTON_PIN 4

void app_main(void) {}

void display_setup(void) {
  // 1. Configure the Output Pin (LED)
  lv_init();

  /* Initialize your hardware. */

  /* hw_init(); */

  demo_create();

  /* Create the UI or start a task for it.
   * In the end, don't forget to call `lv_task_handler` in a loop. */

  /* hw_loop(); */

  return 0;
}

void button_callback(void *arg) {
  // 1. Configure the Output Pin (LED)
  printf("Button pressed\n");

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
    // // 3. Read the input level
    // int btn_state = gpio_get_level(BUTTON_PIN);

    // printf("Button state: %d\n", btn_state);

    // 4. Write that level to the output pin
    // Note: If using PULLUP, btn_state is 0 when pressed
    int btn_state = gpio_get_level(BUTTON_PIN);

    printf("Button state: %d\n", btn_state);
    printf("Hello World\n");

    gpio_set_level(LED_PIN, btn_state);

    printf("LED state: %d\n", gpio_get_level(LED_PIN));

    vTaskDelay(10 /
               portTICK_PERIOD_MS); // Small delay to prevent watchdog issues
  }
}