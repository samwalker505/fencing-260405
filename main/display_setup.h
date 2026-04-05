#pragma once

#include <stdbool.h>

/** LilyGO T-Display S3: parallel I80 ST7789 (320x170) + LVGL. */
void display_setup(void);

/** Thread-safe: updates center status (copies `msg` internally). */
void display_set_main_message(const char *msg);

/** Thread-safe: small title under the top bar (mode name / "Menu"). */
void display_set_title(const char *msg);

/** On-screen hints at top-left / bottom-left; mirrors physical button press (held). */
void display_set_hw_buttons_visual(bool top_pressed, bool bottom_pressed);

/** Menu/home: logo background, title hidden, status line near bottom. Other screens: centered text. */
void display_set_menu_look(bool menu_active);
