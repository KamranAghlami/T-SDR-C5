idf_component_register(SRCS "src/lcd.cpp" "src/buttons.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core esp_driver_gpio esp_lcd esp_timer
    PRIV_REQUIRES esp_driver_spi esp_driver_ledc)
