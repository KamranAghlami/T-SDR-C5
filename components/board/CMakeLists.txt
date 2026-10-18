# The two boards differ in panel bus and battery monitor; buttons are shared.
if(IDF_TARGET STREQUAL "esp32s3")
    set(board_srcs "src/battery_adc.cpp" "src/lcd_i80.cpp")
    set(board_requires esp_adc)
else()
    set(board_srcs "src/battery_axp2602.cpp" "src/lcd_spi.cpp")
    set(board_requires esp_driver_i2c)
endif()

idf_component_register(SRCS ${board_srcs} "src/buttons.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core esp_driver_gpio esp_lcd esp_timer ${board_requires}
    PRIV_REQUIRES esp_driver_spi esp_driver_ledc)
