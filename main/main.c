#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

// Active-low RGB LED (common pin tied to 3.3V):
// Writing 0 to a color pin turns that color ON.
#define LED_R_PIN GPIO_NUM_17
#define LED_G_PIN GPIO_NUM_4
#define LED_B_PIN GPIO_NUM_16
#define BUZZER_PIN GPIO_NUM_5
#define BUTTON_PIN GPIO_NUM_18

// 8-bit PWM resolution.
#define PWM_RES LEDC_TIMER_8_BIT
#define PWM_MAX 255

static void rgb_init(void) {
  const ledc_timer_config_t timer_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = PWM_RES,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = 5000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&timer_cfg);

  const ledc_channel_config_t channels[] = {
      {.gpio_num = LED_R_PIN,
       .speed_mode = LEDC_LOW_SPEED_MODE,
       .channel = LEDC_CHANNEL_0,
       .intr_type = LEDC_INTR_DISABLE,
       .timer_sel = LEDC_TIMER_0,
       .duty = PWM_MAX, // active-low: max duty = off
       .hpoint = 0},
      {.gpio_num = LED_G_PIN,
       .speed_mode = LEDC_LOW_SPEED_MODE,
       .channel = LEDC_CHANNEL_1,
       .intr_type = LEDC_INTR_DISABLE,
       .timer_sel = LEDC_TIMER_0,
       .duty = PWM_MAX,
       .hpoint = 0},
      {.gpio_num = LED_B_PIN,
       .speed_mode = LEDC_LOW_SPEED_MODE,
       .channel = LEDC_CHANNEL_2,
       .intr_type = LEDC_INTR_DISABLE,
       .timer_sel = LEDC_TIMER_0,
       .duty = PWM_MAX,
       .hpoint = 0},
  };

  for (int i = 0; i < 3; i++) {
    ledc_channel_config(&channels[i]);
  }
}

static void button_init(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << BUTTON_PIN,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
}

static void led_init(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << LED_R_PIN | 1ULL << LED_G_PIN | 1ULL << LED_B_PIN,
      .mode = GPIO_MODE_OUTPUT,
  };
  gpio_config(&cfg);
}

static void rgb_set_u8(uint8_t r, uint8_t g, uint8_t b) {
  // Active-low LED: 255=full ON -> duty 0, 0=OFF -> duty 255.
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, PWM_MAX - r);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, PWM_MAX - g);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, PWM_MAX - b);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
}

static void fade_rgb(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1,
                     uint8_t b1, int steps, int step_ms) {
  for (int i = 0; i <= steps; i++) {
    uint8_t r = (uint8_t)(r0 + ((int)(r1 - r0) * i) / steps);
    uint8_t g = (uint8_t)(g0 + ((int)(g1 - g0) * i) / steps);
    uint8_t b = (uint8_t)(b0 + ((int)(b1 - b0) * i) / steps);
    rgb_set_u8(r, g, b);
    vTaskDelay(pdMS_TO_TICKS(step_ms));
  }
}

static void buzzer_init(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << BUZZER_PIN,
      .mode = GPIO_MODE_OUTPUT,
  };
  gpio_config(&cfg);
}

static void buzzer_on(void) { gpio_set_level(BUZZER_PIN, 1); }

static void buzzer_off(void) { gpio_set_level(BUZZER_PIN, 0); }

static void buzzer_beep(void) {
  gpio_set_level(BUZZER_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(200));
  gpio_set_level(BUZZER_PIN, 0);
}

void app_main(void) {
  rgb_init();
  buzzer_init();
  button_init();
  led_init();
  buzzer_beep();
  buzzer_beep();

  gpio_set_level(LED_R_PIN, 1);
  gpio_set_level(LED_G_PIN, 0);
  gpio_set_level(LED_B_PIN, 1);

  while (1) {
    int button_state = gpio_get_level(BUTTON_PIN);
    printf("Button state: %d\n", button_state);
    if (button_state == 1) {
      gpio_set_level(LED_G_PIN, 0);
      gpio_set_level(BUZZER_PIN, 1);
    } else {
      gpio_set_level(LED_G_PIN, 1);
      gpio_set_level(BUZZER_PIN, 0);
    }
    printf("Button state: %d\n", button_state);
    vTaskDelay(pdMS_TO_TICKS(30));
  }

  // while (1) {
  //   // Smoothly transition R -> G -> B, about 1 second each transition.
  //   fade_rgb(255, 0, 0, 0, 255, 0, 100, 10);
  //   fade_rgb(0, 255, 0, 0, 0, 255, 100, 10);
  //   fade_rgb(0, 0, 255, 255, 0, 0, 100, 10);
  // }
}
