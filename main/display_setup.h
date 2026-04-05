#pragma once

#include <stdbool.h>

/** LilyGO T-Display S3: parallel I80 ST7789 (320x170) + LVGL. */
void display_setup(void);

/** Thread-safe: updates center status (copies `msg` internally). */
void display_set_main_message(const char *msg);

/** Thread-safe: small title under the top bar (mode name / "Menu"). */
void display_set_title(const char *msg);

/** Show or hide the title label (off in Training; on in Speed test). */
void display_set_title_visible(bool visible);

/** On-screen hints at top-left / bottom-left; mirrors physical button press (held). */
void display_set_hw_buttons_visual(bool top_pressed, bool bottom_pressed);

/** Menu/home: logo background, title hidden, status line near bottom. Other screens: centered text. */
void display_set_menu_look(bool menu_active);

/** Full screen white + default label colors (after Training red screen, etc.). */
void display_restore_standard_look(void);

/**
 * On-screen hardware button row: `top` full-width bar and bottom corner button.
 * If `top_visible` is false, top bar is hidden (Training exit UI).
 */
void display_set_button_chrome(bool top_visible, const char *top_caption,
                               const char *bottom_caption);

/**
 * Training feedback: released → white bg, black "Ready"; pressed → red bg, white "HIT!".
 */
void display_set_training_view(bool hit_active);
