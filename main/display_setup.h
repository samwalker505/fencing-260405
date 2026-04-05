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

/** Hide speed-test arc + average label (menu, training, or after restore). */
void display_speed_test_ui_hide(void);

/**
 * Speed test: show arc, range 0..target, value 0, clear center status text.
 * Uses LVGL 7 `lv_arc` (value + background angles), not LVGL 9 APIs.
 */
void display_speed_test_ui_begin(int target_hits);

void display_speed_test_ui_set_progress(int hits, int target_hits);

/** Full arc + `%.1f ms` centered inside the ring (no separate overlay label). */
void display_speed_test_ui_show_average_ms(double avg_between_ms);

/** Paused: show "PAUSED" centered inside the ring (same label as the result, not s_main_label). */
void display_speed_test_ui_set_paused(bool paused);
