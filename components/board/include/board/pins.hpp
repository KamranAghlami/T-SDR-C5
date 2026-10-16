// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <driver/gpio.h>

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
