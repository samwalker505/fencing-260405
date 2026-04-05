#pragma once

/** LilyGO T-Display S3: parallel I80 ST7789 (320x170) + LVGL. */
void display_setup(void);

/** Thread-safe: schedules label update on the LVGL task. */
void display_set_pressed_count(int count);
