idf_component_register(SRCS "hello_world_main.c" "display_setup.c"
                       PRIV_REQUIRES spi_flash
                       INCLUDE_DIRS "."
                       REQUIRES lvgl esp_lcd esp_driver_gpio esp_timer freertos)
