// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <driver/gpio.h>
#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32C5
// LilyGO T-Display-C5 (schematic 2026-05-19).
namespace board::pins
{
    inline constexpr gpio_num_t lcd_sck = GPIO_NUM_7;
    inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_9;
    inline constexpr gpio_num_t lcd_dc = GPIO_NUM_8;
    inline constexpr gpio_num_t lcd_cs = GPIO_NUM_26;
    inline constexpr gpio_num_t lcd_rst = GPIO_NUM_23;
    inline constexpr gpio_num_t lcd_backlight = GPIO_NUM_25;

    inline constexpr gpio_num_t button_a = GPIO_NUM_0;  // "USER", active low
    inline constexpr gpio_num_t button_b = GPIO_NUM_28; // "BOOT", active low

    inline constexpr gpio_num_t i2c_sda = GPIO_NUM_2;
    inline constexpr gpio_num_t i2c_scl = GPIO_NUM_3;
}

namespace board
{
    // The C5 also receives 4790-5990 MHz.
    inline constexpr bool dual_band = true;
}
#elif CONFIG_IDF_TARGET_ESP32S3
// LilyGO T-Display-S3 (ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM).
namespace board::pins
{
    // ST7789 on an 8-bit Intel 8080 bus.
    inline constexpr gpio_num_t lcd_d0 = GPIO_NUM_39;
    inline constexpr gpio_num_t lcd_d1 = GPIO_NUM_40;
    inline constexpr gpio_num_t lcd_d2 = GPIO_NUM_41;
    inline constexpr gpio_num_t lcd_d3 = GPIO_NUM_42;
    inline constexpr gpio_num_t lcd_d4 = GPIO_NUM_45;
    inline constexpr gpio_num_t lcd_d5 = GPIO_NUM_46;
    inline constexpr gpio_num_t lcd_d6 = GPIO_NUM_47;
    inline constexpr gpio_num_t lcd_d7 = GPIO_NUM_48;
    inline constexpr gpio_num_t lcd_wr = GPIO_NUM_8;
    inline constexpr gpio_num_t lcd_rd = GPIO_NUM_9; // held high: the panel is write-only here
    inline constexpr gpio_num_t lcd_dc = GPIO_NUM_7;
    inline constexpr gpio_num_t lcd_cs = GPIO_NUM_6;
    inline constexpr gpio_num_t lcd_rst = GPIO_NUM_5;
    inline constexpr gpio_num_t lcd_backlight = GPIO_NUM_38;
    // Powers the panel when running from the battery.
    inline constexpr gpio_num_t lcd_power = GPIO_NUM_15;

    inline constexpr gpio_num_t button_a = GPIO_NUM_0;  // "BOOT", active low
    inline constexpr gpio_num_t button_b = GPIO_NUM_14; // "KEY", active low

    // Battery voltage through a 1:2 divider (ADC1 channel 3).
    inline constexpr gpio_num_t battery_adc = GPIO_NUM_4;
}

namespace board
{
    // The S3 receives 2.4 GHz only.
    inline constexpr bool dual_band = false;
}
#else
#error "No board definition for this target"
#endif
