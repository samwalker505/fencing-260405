#include "screen_training.h"

#include "display_setup.h"
#include <stdio.h>

static bool s_show_hit;

void screen_training_on_enter(app_context_t *ctx, const app_input_t *in) {
  (void)ctx;
  s_show_hit = (in != NULL && in->fencing_level != 0);

  display_set_main_message("");
  display_speed_test_ui_hide();
  display_restore_standard_look();
  display_set_menu_look(false);
  display_set_title_visible(false);
  display_set_button_chrome(false, "", "X");
  display_set_training_view(s_show_hit);
  printf("training mode\n");
}

app_screen_t screen_training_update(app_context_t *ctx, const app_input_t *in) {
  (void)ctx;

  if (in->next_press) {
    return APP_SCREEN_MENU;
  }

  const bool hit = (in->fencing_level != 0);
  if (hit != s_show_hit) {
    s_show_hit = hit;
    display_set_training_view(hit);
  }

  return APP_SCREEN_TRAINING;
}
