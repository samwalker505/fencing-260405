#include "display_setup.h"
#include "app_config.h"
#include "walker_splash.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static lv_obj_t *s_main_label;
static lv_obj_t *s_title_label;
static lv_obj_t *s_menu_bg_img;
static lv_obj_t *s_vis_btn_top;
static lv_obj_t *s_vis_btn_bottom;
static lv_obj_t *s_vis_btn_top_lbl;
static lv_obj_t *s_vis_btn_bottom_lbl;
#if defined(LV_USE_ARC) && LV_USE_ARC
static lv_obj_t *s_speed_arc;
static lv_obj_t *s_speed_avg_lbl;
#endif

static bool s_have_hw_vis;
static bool s_last_hw_top;
static bool s_last_hw_bottom;
#endif

static void async_main_message(void *user_data) {
#if SOC_LCD_I80_SUPPORTED
  char *copy = (char *)user_data;
  if (s_main_label && copy) {
    lv_label_set_long_mode(s_main_label, LV_LABEL_LONG_BREAK);
    lv_obj_set_width(s_main_label, LCD_H_RES - 24);
    lv_label_set_text(s_main_label, copy);
  }
  free(copy);
#endif
}

void display_set_main_message(const char *msg) {
#if SOC_LCD_I80_SUPPORTED
  if (s_main_label == NULL || msg == NULL) {
    return;
  }
  size_t n = strlen(msg) + 1;
  char *copy = (char *)malloc(n);
  if (copy == NULL) {
    return;
  }
  memcpy(copy, msg, n);
  lv_async_call(async_main_message, copy);
#endif
}

static void async_title_message(void *user_data) {
#if SOC_LCD_I80_SUPPORTED
  char *copy = (char *)user_data;
  if (s_title_label && copy) {
    lv_label_set_text(s_title_label, copy);
  }
  free(copy);
#endif
}

void display_set_title(const char *msg) {
#if SOC_LCD_I80_SUPPORTED
  if (s_title_label == NULL || msg == NULL) {
    return;
  }
  size_t n = strlen(msg) + 1;
  char *copy = (char *)malloc(n);
  if (copy == NULL) {
    return;
  }
  memcpy(copy, msg, n);
  lv_async_call(async_title_message, copy);
#endif
}

#if SOC_LCD_I80_SUPPORTED
static void async_hw_btn_visual(void *user_data) {
  uintptr_t v = (uintptr_t)user_data;
  bool top = (v & 1u) != 0;
  bool bottom = (v & 2u) != 0;
  if (s_vis_btn_top) {
    lv_btn_set_state(s_vis_btn_top, top ? LV_BTN_STATE_PRESSED : LV_BTN_STATE_RELEASED);
  }
  if (s_vis_btn_bottom) {
    lv_btn_set_state(s_vis_btn_bottom, bottom ? LV_BTN_STATE_PRESSED : LV_BTN_STATE_RELEASED);
  }
}
#endif

void display_set_hw_buttons_visual(bool top_pressed, bool bottom_pressed) {
#if SOC_LCD_I80_SUPPORTED
  if (s_vis_btn_top == NULL || s_vis_btn_bottom == NULL) {
    return;
  }
  if (s_have_hw_vis && top_pressed == s_last_hw_top && bottom_pressed == s_last_hw_bottom) {
    return;
  }
  s_have_hw_vis = true;
  s_last_hw_top = top_pressed;
  s_last_hw_bottom = bottom_pressed;
  uintptr_t v = (top_pressed ? 1u : 0u) | (bottom_pressed ? 2u : 0u);
  lv_async_call(async_hw_btn_visual, (void *)v);
#else
  (void)top_pressed;
  (void)bottom_pressed;
#endif
}

typedef struct {
  bool menu_active;
} menu_look_msg_t;

static void async_menu_look(void *user_data) {
#if SOC_LCD_I80_SUPPORTED
  menu_look_msg_t *m = (menu_look_msg_t *)user_data;
  if (m == NULL) {
    return;
  }
  const bool on = m->menu_active;
  free(m);

  if (s_menu_bg_img) {
    lv_obj_set_hidden(s_menu_bg_img, !on);
    if (on) {
      lv_obj_move_background(s_menu_bg_img);
    }
  }
  if (s_title_label) {
    if (on) {
      lv_obj_set_hidden(s_title_label, true);
    }
    /* Leaving menu: do not unhide title (Training keeps it off; Speed turns it on). */
  }
  if (s_main_label) {
    if (on) {
      lv_obj_align(s_main_label, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -38);
    } else {
      lv_obj_align(s_main_label, NULL, LV_ALIGN_CENTER, 0, 0);
    }
  }
#endif
}

void display_set_menu_look(bool menu_active) {
#if SOC_LCD_I80_SUPPORTED
  menu_look_msg_t *m = (menu_look_msg_t *)malloc(sizeof(*m));
  if (m == NULL) {
    return;
  }
  m->menu_active = menu_active;
  lv_async_call(async_menu_look, m);
#else
  (void)menu_active;
#endif
}

static void async_title_visible(void *user_data) {
#if SOC_LCD_I80_SUPPORTED
  bool vis = ((uintptr_t)user_data) != 0;
  if (s_title_label) {
    lv_obj_set_hidden(s_title_label, !vis);
  }
#endif
}

void display_set_title_visible(bool visible) {
#if SOC_LCD_I80_SUPPORTED
  lv_async_call(async_title_visible, (void *)(uintptr_t)(visible ? 1u : 0));
#else
  (void)visible;
#endif
}

#if SOC_LCD_I80_SUPPORTED && defined(LV_USE_ARC) && LV_USE_ARC
static void speed_test_ui_hide_sync(void) {
  if (s_speed_arc) {
    lv_obj_set_hidden(s_speed_arc, true);
  }
  if (s_speed_avg_lbl) {
    lv_label_set_text(s_speed_avg_lbl, "");
    lv_obj_set_hidden(s_speed_avg_lbl, true);
  }
}
#endif

static void async_restore_standard(void *user_data) {
  (void)user_data;
#if SOC_LCD_I80_SUPPORTED
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
  if (s_main_label) {
    lv_obj_set_hidden(s_main_label, false);
    lv_label_set_long_mode(s_main_label, LV_LABEL_LONG_BREAK);
    lv_obj_set_width(s_main_label, LCD_H_RES - 24);
    lv_obj_set_style_local_text_color(s_main_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                      LV_COLOR_BLACK);
#if LV_FONT_MONTSERRAT_16
    lv_obj_set_style_local_text_font(s_main_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                     &lv_font_montserrat_16);
#endif
    lv_obj_align(s_main_label, NULL, LV_ALIGN_CENTER, 0, 0);
  }
#endif
}

void display_restore_standard_look(void) {
#if SOC_LCD_I80_SUPPORTED
  lv_async_call(async_restore_standard, NULL);
#endif
}

typedef struct {
  bool top_visible;
  char top[48];
  char bottom[16];
} chrome_msg_t;

static void async_button_chrome(void *user_data) {
#if SOC_LCD_I80_SUPPORTED
  chrome_msg_t *m = (chrome_msg_t *)user_data;
  if (m == NULL) {
    return;
  }
  if (s_vis_btn_top) {
    lv_obj_set_hidden(s_vis_btn_top, !m->top_visible);
  }
  if (s_vis_btn_top_lbl) {
    lv_label_set_text(s_vis_btn_top_lbl, m->top);
  }
  if (s_vis_btn_bottom_lbl) {
    lv_label_set_text(s_vis_btn_bottom_lbl, m->bottom);
  }
  free(m);
#endif
}

void display_set_button_chrome(bool top_visible, const char *top_caption,
                               const char *bottom_caption) {
#if SOC_LCD_I80_SUPPORTED
  chrome_msg_t *m = (chrome_msg_t *)malloc(sizeof(*m));
  if (m == NULL) {
    return;
  }
  m->top_visible = top_visible;
  snprintf(m->top, sizeof m->top, "%s", top_caption ? top_caption : "");
  snprintf(m->bottom, sizeof m->bottom, "%s", bottom_caption ? bottom_caption : "");
  lv_async_call(async_button_chrome, m);
#else
  (void)top_visible;
  (void)top_caption;
  (void)bottom_caption;
#endif
}

static void async_training_view(void *user_data) {
#if SOC_LCD_I80_SUPPORTED
  const bool hit = ((uintptr_t)user_data) != 0;
  lv_obj_t *scr = lv_scr_act();
  if (s_title_label) {
    lv_obj_set_hidden(s_title_label, true);
  }
  if (hit) {
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
    lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    if (s_main_label) {
      lv_obj_set_hidden(s_main_label, false);
      lv_label_set_long_mode(s_main_label, LV_LABEL_LONG_EXPAND);
      lv_obj_set_style_local_text_color(s_main_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                        LV_COLOR_WHITE);
#if LV_FONT_MONTSERRAT_40
      lv_obj_set_style_local_text_font(s_main_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                       &lv_font_montserrat_40);
#elif LV_FONT_MONTSERRAT_28
      lv_obj_set_style_local_text_font(s_main_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                       &lv_font_montserrat_28);
#endif
      lv_label_set_text(s_main_label, "HIT!");
      lv_label_set_align(s_main_label, LV_LABEL_ALIGN_CENTER);
      lv_obj_align(s_main_label, NULL, LV_ALIGN_CENTER, 0, 0);
    }
  } else {
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    if (s_main_label) {
      lv_obj_set_hidden(s_main_label, false);
      lv_label_set_long_mode(s_main_label, LV_LABEL_LONG_BREAK);
      lv_obj_set_width(s_main_label, LCD_H_RES - 24);
      lv_label_set_text(s_main_label, "Ready");
      lv_obj_set_style_local_text_color(s_main_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                        LV_COLOR_BLACK);
#if LV_FONT_MONTSERRAT_16
      lv_obj_set_style_local_text_font(s_main_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                       &lv_font_montserrat_16);
#endif
      lv_obj_align(s_main_label, NULL, LV_ALIGN_CENTER, 0, 0);
    }
  }
#endif
}

void display_set_training_view(bool hit_active) {
#if SOC_LCD_I80_SUPPORTED
  lv_async_call(async_training_view, (void *)(uintptr_t)(hit_active ? 1u : 0));
#else
  (void)hit_active;
#endif
}

#if SOC_LCD_I80_SUPPORTED && defined(LV_USE_ARC) && LV_USE_ARC

/**
 * Semicircular loader (∩ opening downward per sketch): BG track + INDIC progress, 0..target steps.
 * LVGL angles: 0° = 3 o'clock; 180→360° is the upper semicircle (curve on top, opening below).
 */
static void speed_loader_apply_ring_style(lv_obj_t *arc) {
  lv_arc_set_rotation(arc, 0);
  lv_arc_set_bg_angles(arc, 180, 360);
  lv_arc_set_type(arc, LV_ARC_TYPE_NORMAL);

  lv_obj_set_style_local_border_width(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_left(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_right(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_top(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_bottom(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_inner(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);

  lv_obj_set_style_local_line_width(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 8);
  lv_obj_set_style_local_line_color(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT,
                                    LV_COLOR_MAKE(0xe4, 0xe4, 0xe8));
  lv_obj_set_style_local_line_width(arc, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, 7);
  lv_obj_set_style_local_line_color(arc, LV_ARC_PART_INDIC, LV_STATE_DEFAULT,
                                    LV_COLOR_MAKE(0x00, 0x5a, 0xb8));
  lv_obj_set_style_local_line_width(arc, LV_ARC_PART_KNOB, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_border_width(arc, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, 0);
}

static void speed_arc_layout_centered_screen(void) {
  if (s_speed_arc == NULL) {
    return;
  }
  lv_obj_align(s_speed_arc, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
}

static void speed_avg_place_in_arc_opening(void) {
  if (s_speed_avg_lbl == NULL || s_speed_arc == NULL) {
    return;
  }
  /*
   * Inside the widget below the drawn ∩ (semicircle is in the upper half). Avoids the indicator
   * paint covering LV_ALIGN_CENTER, and works with a normal fixed-size label.
   */
  lv_obj_align(s_speed_avg_lbl, s_speed_arc, LV_ALIGN_IN_BOTTOM_MID, 0, -6);
}

static void speed_avg_label_style_result(void) {
  if (s_speed_avg_lbl == NULL) {
    return;
  }
  lv_label_set_long_mode(s_speed_avg_lbl, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(s_speed_avg_lbl, LCD_H_RES - 24);
  lv_label_set_align(s_speed_avg_lbl, LV_LABEL_ALIGN_CENTER);
  lv_obj_set_style_local_text_color(s_speed_avg_lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                    LV_COLOR_BLACK);
  lv_obj_set_style_local_bg_opa(s_speed_avg_lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(s_speed_avg_lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, 0);
#if LV_FONT_MONTSERRAT_16
  lv_obj_set_style_local_text_font(s_speed_avg_lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                   &lv_font_montserrat_16);
#endif
}

/** Keep average / PAUSED text above the arc in z-order (arc move_foreground must not run after). */
static void speed_avg_raise(void) {
  if (s_speed_avg_lbl) {
    lv_obj_move_foreground(s_speed_avg_lbl);
    lv_obj_invalidate(s_speed_avg_lbl);
  }
}

static void speed_arc_ensure_visible(void) {
  if (s_speed_arc) {
    lv_obj_set_hidden(s_speed_arc, false);
    speed_arc_layout_centered_screen();
    lv_obj_move_foreground(s_speed_arc);
    /* If result/PAUSED is visible, it must stay after the arc in the child list. */
    if (s_speed_avg_lbl) {
      const char *t = lv_label_get_text(s_speed_avg_lbl);
      if (t != NULL && t[0] != '\0') {
        speed_avg_raise();
      }
    }
  }
}

typedef struct {
  int16_t hits;
  int16_t target;
} speed_progress_msg_t;

typedef struct {
  double avg_ms;
} speed_avg_msg_t;

static void async_speed_ui_hide(void *user_data) {
  (void)user_data;
  speed_test_ui_hide_sync();
}

void display_speed_test_ui_hide(void) {
  lv_async_call(async_speed_ui_hide, NULL);
}

static void async_speed_ui_begin(void *user_data) {
  int *target = (int *)user_data;
  if (target == NULL || s_speed_arc == NULL) {
    free(target);
    return;
  }
  const int t = *target;
  free(target);

  if (s_main_label) {
    lv_label_set_long_mode(s_main_label, LV_LABEL_LONG_BREAK);
    lv_obj_set_width(s_main_label, LCD_H_RES - 24);
    lv_label_set_text(s_main_label, "");
    lv_obj_set_hidden(s_main_label, true);
  }
  if (s_speed_avg_lbl) {
    lv_label_set_text(s_speed_avg_lbl, "");
    lv_obj_set_hidden(s_speed_avg_lbl, true);
  }

  speed_loader_apply_ring_style(s_speed_arc);
  lv_arc_set_range(s_speed_arc, 0, (int16_t)t);
  lv_arc_set_value(s_speed_arc, 0);
  speed_arc_ensure_visible();
}

void display_speed_test_ui_begin(int target_hits) {
  if (target_hits <= 0) {
    return;
  }
  int *p = (int *)malloc(sizeof(*p));
  if (p == NULL) {
    return;
  }
  *p = target_hits;
  lv_async_call(async_speed_ui_begin, p);
}

static void async_speed_ui_set_progress(void *user_data) {
  speed_progress_msg_t *m = (speed_progress_msg_t *)user_data;
  if (m == NULL || s_speed_arc == NULL) {
    free(m);
    return;
  }
  if (lv_arc_get_max_value(s_speed_arc) != m->target) {
    lv_arc_set_range(s_speed_arc, 0, m->target);
  }
  int16_t v = m->hits;
  if (v < 0) {
    v = 0;
  }
  if (v > m->target) {
    v = m->target;
  }
  lv_arc_set_value(s_speed_arc, v);
  speed_arc_ensure_visible();
  free(m);
}

void display_speed_test_ui_set_progress(int hits, int target_hits) {
  if (target_hits <= 0) {
    return;
  }
  speed_progress_msg_t *m = (speed_progress_msg_t *)malloc(sizeof(*m));
  if (m == NULL) {
    return;
  }
  m->hits = (int16_t)hits;
  m->target = (int16_t)target_hits;
  lv_async_call(async_speed_ui_set_progress, m);
}

static void async_speed_ui_show_avg(void *user_data) {
  speed_avg_msg_t *m = (speed_avg_msg_t *)user_data;
  if (m == NULL) {
    return;
  }
  char buf[32];
  snprintf(buf, sizeof buf, "%.1f ms", m->avg_ms);
  free(m);

  if (s_speed_arc == NULL || s_speed_avg_lbl == NULL) {
    return;
  }
  if (s_main_label) {
    lv_obj_set_hidden(s_main_label, true);
  }
  lv_arc_set_value(s_speed_arc, lv_arc_get_max_value(s_speed_arc));
  speed_arc_ensure_visible();

  speed_avg_label_style_result();
  lv_label_set_text(s_speed_avg_lbl, buf);
  lv_obj_set_hidden(s_speed_avg_lbl, false);
  speed_avg_place_in_arc_opening();
  lv_obj_move_foreground(s_speed_arc);
  speed_avg_raise();
}

void display_speed_test_ui_show_average_ms(double avg_between_ms) {
  speed_avg_msg_t *m = (speed_avg_msg_t *)malloc(sizeof(*m));
  if (m == NULL) {
    return;
  }
  m->avg_ms = avg_between_ms;
  lv_async_call(async_speed_ui_show_avg, m);
}

static void async_speed_ui_set_paused(void *user_data) {
  const bool paused = ((uintptr_t)user_data) != 0;
  if (s_speed_arc == NULL || s_speed_avg_lbl == NULL) {
    return;
  }
  speed_arc_ensure_visible();
  if (paused) {
    speed_avg_label_style_result();
    lv_label_set_text(s_speed_avg_lbl, "PAUSED");
    lv_obj_set_hidden(s_speed_avg_lbl, false);
    speed_avg_place_in_arc_opening();
    lv_obj_move_foreground(s_speed_arc);
    speed_avg_raise();
  } else {
    lv_label_set_text(s_speed_avg_lbl, "");
    lv_obj_set_hidden(s_speed_avg_lbl, true);
  }
}

void display_speed_test_ui_set_paused(bool paused) {
  lv_async_call(async_speed_ui_set_paused, (void *)(uintptr_t)(paused ? 1u : 0));
}

#else /* !LV_USE_ARC */

void display_speed_test_ui_hide(void) {}

void display_speed_test_ui_begin(int target_hits) {
  (void)target_hits;
}

void display_speed_test_ui_set_progress(int hits, int target_hits) {
  (void)hits;
  (void)target_hits;
}

void display_speed_test_ui_show_average_ms(double avg_between_ms) {
  (void)avg_between_ms;
}

void display_speed_test_ui_set_paused(bool paused) { (void)paused; }

#endif /* LV_USE_ARC */

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

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

  s_title_label = lv_label_create(scr, NULL);
  lv_label_set_text(s_title_label, "");
  lv_obj_align(s_title_label, NULL, LV_ALIGN_IN_TOP_MID, 0, 36);

  /* On-screen controls aligned with physical buttons (left edge, top / bottom). */
  const lv_coord_t pad = 6;
  const lv_coord_t bw_top = LCD_H_RES - 2 * pad;
  const lv_coord_t bh_top = 34;
  const lv_coord_t bw_bot = 52;
  const lv_coord_t bh_bot = 30;

  s_vis_btn_top = lv_btn_create(scr, NULL);
  lv_obj_set_size(s_vis_btn_top, bw_top, bh_top);
  lv_obj_align(s_vis_btn_top, NULL, LV_ALIGN_IN_TOP_LEFT, pad, pad);
  lv_obj_set_click(s_vis_btn_top, false);
  lv_obj_t *lt = lv_label_create(s_vis_btn_top, NULL);
  lv_label_set_long_mode(lt, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(lt, bw_top - 10);
  lv_label_set_align(lt, LV_LABEL_ALIGN_CENTER);
  lv_label_set_text(lt, "Speed Test Mode");
  lv_obj_align(lt, NULL, LV_ALIGN_CENTER, 0, 0);
  s_vis_btn_top_lbl = lt;

  s_vis_btn_bottom = lv_btn_create(scr, NULL);
  lv_obj_set_size(s_vis_btn_bottom, bw_bot, bh_bot);
  lv_obj_align(s_vis_btn_bottom, NULL, LV_ALIGN_IN_BOTTOM_LEFT, pad, -pad);
  lv_obj_set_click(s_vis_btn_bottom, false);
  lv_obj_t *lb = lv_label_create(s_vis_btn_bottom, NULL);
  lv_label_set_text(lb, "next");
  lv_obj_align(lb, NULL, LV_ALIGN_CENTER, 0, 0);
  s_vis_btn_bottom_lbl = lb;

  s_main_label = lv_label_create(scr, NULL);
  lv_label_set_long_mode(s_main_label, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(s_main_label, LCD_H_RES - 24);
  lv_label_set_text(s_main_label, "");
  lv_obj_align(s_main_label, NULL, LV_ALIGN_CENTER, 0, 0);

#if defined(LV_USE_ARC) && LV_USE_ARC
  s_speed_arc = lv_arc_create(scr, NULL);
  /* Large semicircle, horizontally centered with full-width top bar (OUT_BOTTOM_MID). */
  lv_obj_set_size(s_speed_arc, 252, 86);
  lv_arc_set_range(s_speed_arc, 0, APP_SPEED_TARGET_HITS);
  lv_arc_set_value(s_speed_arc, 0);
  lv_arc_set_adjustable(s_speed_arc, false);
  lv_obj_set_click(s_speed_arc, false);
  speed_loader_apply_ring_style(s_speed_arc);
  lv_obj_align(s_speed_arc, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_hidden(s_speed_arc, true);

  /*
   * Sibling on screen (not child of arc): LVGL 7 draws the arc on top of arc children, which hid
   * the result. Align to arc center and move_foreground so text paints above the ring.
   */
  s_speed_avg_lbl = lv_label_create(scr, NULL);
  lv_label_set_text(s_speed_avg_lbl, "");
  speed_avg_label_style_result();
  speed_avg_place_in_arc_opening();
  lv_obj_set_hidden(s_speed_avg_lbl, true);
#endif

  s_menu_bg_img = lv_img_create(scr, NULL);
  lv_img_set_src(s_menu_bg_img, &walker_splash);
  /* Below full-width top bar so the monogram stays visible */
  lv_obj_align(s_menu_bg_img, NULL, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_hidden(s_menu_bg_img, true);
  lv_obj_move_background(s_menu_bg_img);

  xTaskCreate(lvgl_port_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL);
  ESP_LOGI(TAG, "T-Display S3 (I80 ST7789) ready %dx%d", LCD_H_RES, LCD_V_RES);
#endif
}
