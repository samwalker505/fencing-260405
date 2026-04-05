idf_component_register(SRCS "hello_world_main.c"
                            "app_hw.c"
                            "screen_menu.c"
                            "screen_speed_test.c"
                            "screen_training.c"
                            "display_setup.c"
                            "walker_splash.c"
                       PRIV_REQUIRES spi_flash
                       INCLUDE_DIRS "."
                       REQUIRES lvgl esp_lcd esp_driver_gpio esp_timer freertos)
