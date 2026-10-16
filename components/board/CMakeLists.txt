idf_component_register(SRCS "src/battery.cpp" "src/buttons.cpp" "src/lcd.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core esp_driver_gpio esp_driver_i2c esp_lcd esp_timer
    PRIV_REQUIRES esp_driver_spi esp_driver_ledc)
