#include "screen_menu.h"

#include "display_setup.h"
#include <stdio.h>

void screen_menu_on_enter(app_context_t *ctx) {
  (void)ctx;
  display_set_menu_look(true);
  display_set_title("");
  // menu_paint(ctx);
}

app_screen_t screen_menu_update(app_context_t *ctx, const app_input_t *in) {
  if (in->next_press) {
    ctx->menu_selected = (ctx->menu_selected + 1) % APP_MENU_MODE_COUNT;
    printf("menu sel %d\n", ctx->menu_selected);
  }

  if (in->go_press && ctx->menu_selected == APP_MENU_MODE_SPEED_TEST) {
    return APP_SCREEN_SPEED_TEST;
  }

  return APP_SCREEN_MENU;
}
