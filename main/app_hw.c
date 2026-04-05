#include "app_hw.h"

#include "app_config.h"
#include "display_setup.h"
#include "driver/gpio.h"

void app_hw_init(void) {
  const gpio_config_t btn = {
      .pin_bit_mask = (1ULL << APP_BUTTON_GO_GPIO) | (1ULL << APP_BUTTON_NEXT_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&btn);

  const gpio_config_t led = {
      .pin_bit_mask = 1ULL << APP_LED_GPIO,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&led);

  const gpio_config_t fence = {
      .pin_bit_mask = 1ULL << APP_FENCING_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&fence);
}

void app_hw_poll_input(app_input_t *out, int *prev_top, int *prev_bottom,
                       int *prev_fencing) {
  int top = gpio_get_level(APP_BUTTON_GO_GPIO);
  int bot = gpio_get_level(APP_BUTTON_NEXT_GPIO);
  int fp = gpio_get_level(APP_FENCING_GPIO);

  display_set_hw_buttons_visual(top == 0, bot == 0);
  gpio_set_level(APP_LED_GPIO, fp);

  out->go_press = (top == 0 && *prev_top == 1);
  out->next_press = (bot == 0 && *prev_bottom == 1);
  out->fencing_rising = (fp == 1 && *prev_fencing == 0);
  out->fencing_level = fp;

  *prev_top = top;
  *prev_bottom = bot;
  *prev_fencing = fp;
}
