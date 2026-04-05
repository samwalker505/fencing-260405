#include "display_setup.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "soc/clk_tree_defs.h"
#include "soc/soc_caps.h"

static const char *TAG = "display";

/*
 * LilyGO T-Display S3 — Intel 8080 parallel ST7789 (factory / pin_config.h).
 * SPI on GPIO 11/12/… is a different revision; this matches the common parallel board.
 */
#define PIN_LCD_POWER_ON       15 /* Must be HIGH or panel stays off (battery / rail) */
#define PIN_LCD_RD             9  /* Tie high: read disabled */

#define PIN_LCD_D0             39
#define PIN_LCD_D1             40
#define PIN_LCD_D2             41
#define PIN_LCD_D3             42
#define PIN_LCD_D4             45
#define PIN_LCD_D5             46
#define PIN_LCD_D6             47
#define PIN_LCD_D7             48

#define PIN_LCD_WR             8
#define PIN_LCD_CS             6
#define PIN_LCD_DC             7
#define PIN_LCD_RES            5
#define PIN_LCD_BL             38

#define LCD_H_RES              320
#define LCD_V_RES              170
#define LCD_PIXEL_CLOCK_HZ     (16 * 1000 * 1000)

#define LVGL_TICK_MS           2
#define LVGL_TASK_STACK        (6 * 1024)
#define LVGL_TASK_PRIO         2
#define LVGL_BUF_LINES         40

#define LCD_INIT_DELAY_FLAG    0x80

typedef struct {
  uint8_t cmd;
  uint8_t data[14];
  uint8_t len;
} lcd_cmd_t;

/* Same vendor table as LilyGO factory (LCD_MODULE_CMD_1). */
static const lcd_cmd_t s_lcd_st7789v[] = {
    {0x11, {0}, 0 | LCD_INIT_DELAY_FLAG},
    {0x3A, {0x05}, 1},
    {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x28}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x1F}, 1},
    {0xC6, {0x13}, 1},
    {0xD0, {0xA7}, 1},
    {0xD0, {0xA4, 0xA1}, 2},
    {0xD6, {0xA1}, 1},
    {0xE0, {0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14},
    {0xE1, {0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14},
};

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static lv_disp_drv_t s_disp_drv;

#if SOC_LCD_I80_SUPPORTED
static lv_obj_t *s_count_label;
#endif

static void async_set_count_text(void *user_data) {
#if SOC_LCD_I80_SUPPORTED
  int c = (int)(intptr_t)user_data;
  char buf[48];
  snprintf(buf, sizeof buf, "Presses: %d", c);
  if (s_count_label) {
    lv_label_set_text(s_count_label, buf);
  }
#endif
}

void display_set_pressed_count(int count) {
#if SOC_LCD_I80_SUPPORTED
  if (s_count_label == NULL) {
    return;
  }
  lv_async_call(async_set_count_text, (void *)(intptr_t)count);
#else
  (void)count;
#endif
}

static bool lvgl_on_flush_done(esp_lcd_panel_io_handle_t panel_io,
                               esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
  lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
  lv_disp_flush_ready(drv);
  return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
  (void)drv;
  int x1 = area->x1;
  int y1 = area->y1;
  int x2 = area->x2 + 1;
  int y2 = area->y2 + 1;
  esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, color_map);
}

static void lvgl_tick_timer_cb(void *arg) { lv_tick_inc(LVGL_TICK_MS); }

static void lvgl_port_task(void *arg) {
  const TickType_t delay = pdMS_TO_TICKS(10);
  while (1) {
    lv_task_handler();
    vTaskDelay(delay);
  }
}

#if SOC_LCD_I80_SUPPORTED
static void send_lcd_init_table(void) {
  for (size_t i = 0; i < sizeof(s_lcd_st7789v) / sizeof(s_lcd_st7789v[0]); i++) {
    uint8_t dlen = s_lcd_st7789v[i].len & 0x7f;
    esp_lcd_panel_io_tx_param(s_io, s_lcd_st7789v[i].cmd,
                              dlen ? s_lcd_st7789v[i].data : NULL, dlen);
    if (s_lcd_st7789v[i].len & LCD_INIT_DELAY_FLAG) {
      vTaskDelay(pdMS_TO_TICKS(120));
    }
  }
}
#endif

void display_setup(void) {
#if !SOC_LCD_I80_SUPPORTED
  ESP_LOGE(TAG, "SoC has no LCD I80 — use esp32s3 target for T-Display S3");
  return;
#else
  gpio_config_t io_pwr = {
      .pin_bit_mask = (1ULL << PIN_LCD_POWER_ON) | (1ULL << PIN_LCD_RD),
      .mode = GPIO_MODE_OUTPUT,
  };
  ESP_ERROR_CHECK(gpio_config(&io_pwr));
  gpio_set_level(PIN_LCD_POWER_ON, 1);
  gpio_set_level(PIN_LCD_RD, 1);

  gpio_config_t bl = {
      .pin_bit_mask = 1ULL << PIN_LCD_BL,
      .mode = GPIO_MODE_OUTPUT,
  };
  ESP_ERROR_CHECK(gpio_config(&bl));
  gpio_set_level(PIN_LCD_BL, 1);

  esp_lcd_i80_bus_handle_t i80_bus = NULL;
  esp_lcd_i80_bus_config_t bus_config = {
      .dc_gpio_num = PIN_LCD_DC,
      .wr_gpio_num = PIN_LCD_WR,
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .data_gpio_nums =
          {
              PIN_LCD_D0,
              PIN_LCD_D1,
              PIN_LCD_D2,
              PIN_LCD_D3,
              PIN_LCD_D4,
              PIN_LCD_D5,
              PIN_LCD_D6,
              PIN_LCD_D7,
          },
      .bus_width = 8,
      .max_transfer_bytes = LCD_H_RES * LVGL_BUF_LINES * sizeof(uint16_t),
      .dma_burst_size = 64,
  };
  ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

  lv_disp_drv_init(&s_disp_drv);

  esp_lcd_panel_io_i80_config_t io_config = {
      .cs_gpio_num = PIN_LCD_CS,
      .pclk_hz = LCD_PIXEL_CLOCK_HZ,
      .trans_queue_depth = 20,
      .on_color_trans_done = lvgl_on_flush_done,
      .user_ctx = &s_disp_drv,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .dc_levels =
          {
              .dc_idle_level = 0,
              .dc_cmd_level = 0,
              .dc_dummy_level = 0,
              .dc_data_level = 1,
          },
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &s_io));

  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = PIN_LCD_RES,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
      .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
  /* USB on the left when facing the screen (LilyGO factory default). */
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, true));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 0, 35));

  send_lcd_init_table();

  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

  lv_init();

  size_t buf_bytes = LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color_t);
  void *buf1 = esp_lcd_i80_alloc_draw_buffer(s_io, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *buf2 = esp_lcd_i80_alloc_draw_buffer(s_io, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (buf1 == NULL || buf2 == NULL) {
    ESP_LOGE(TAG, "Failed to allocate I80 draw buffers");
    abort();
  }

  static lv_disp_buf_t disp_buf;
  lv_disp_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_LINES);

  s_disp_drv.hor_res = LCD_H_RES;
  s_disp_drv.ver_res = LCD_V_RES;
  s_disp_drv.flush_cb = lvgl_flush_cb;
  s_disp_drv.buffer = &disp_buf;
  lv_disp_drv_register(&s_disp_drv);

  const esp_timer_create_args_t tick_args = {
      .callback = &lvgl_tick_timer_cb,
      .name = "lvgl_tick",
  };
  esp_timer_handle_t tick = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick));
  ESP_ERROR_CHECK(esp_timer_start_periodic(tick, (int64_t)LVGL_TICK_MS * 1000));

  lv_obj_t *title = lv_label_create(lv_scr_act(), NULL);
  lv_label_set_text(title, "Button");
  lv_obj_align(title, NULL, LV_ALIGN_IN_TOP_MID, 0, 8);

  s_count_label = lv_label_create(lv_scr_act(), NULL);
  lv_label_set_text(s_count_label, "Presses: 0");
  lv_obj_align(s_count_label, NULL, LV_ALIGN_CENTER, 0, 0);

  xTaskCreate(lvgl_port_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL);
  ESP_LOGI(TAG, "T-Display S3 (I80 ST7789) ready %dx%d", LCD_H_RES, LCD_V_RES);
#endif
}
