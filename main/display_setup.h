#pragma once

#include <stdbool.h>

/** LilyGO T-Display S3: parallel I80 ST7789 (320x170) + LVGL. */
void display_setup(void);

/** Thread-safe: updates center status (copies `msg` internally). */
void display_set_main_message(const char *msg);

/** On-screen hints at top-left / bottom-left; mirrors physical button press (held). */
void display_set_hw_buttons_visual(bool top_pressed, bool bottom_pressed);
