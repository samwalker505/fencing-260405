#pragma once

#include <stdint.h>

typedef enum {
  APP_SCREEN_MENU = 0,
  APP_SCREEN_SPEED_TEST,
} app_screen_t;

/* Menu row index (extend when adding modes). */
typedef enum {
  APP_MENU_MODE_SPEED_TEST = 0,
  APP_MENU_MODE_COUNT = 1,
} app_menu_mode_t;

typedef enum {
  APP_SPEED_RUNNING = 0,
  APP_SPEED_PAUSED,
  APP_SPEED_DONE,
} app_speed_phase_t;

typedef struct {
  app_screen_t screen;

  int menu_selected; /* 0 .. APP_MENU_MODE_COUNT-1 */

  app_speed_phase_t speed_phase;
  int64_t last_hit_us;
  uint32_t sum_inter_hit_us;
  int n_hits;
} app_context_t;
