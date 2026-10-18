// SPDX-License-Identifier: GPL-3.0-or-later
#include "board/lcd.hpp"

#include <driver/ledc.h>
#include <esp_check.h>
#include <esp_lcd_io_i80.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>

#include "board/pins.hpp"

namespace board
{
    namespace
    {
        // LovyanGFX drives this panel at 20 MHz on the same board.
        constexpr unsigned pixel_clock_hz = 20'000'000;
        constexpr int max_transfer_rows = 16;
        constexpr ledc_channel_t backlight_channel = LEDC_CHANNEL_0;
        constexpr ledc_timer_t backlight_timer = LEDC_TIMER_0;
    }

    esp_err_t lcd::init()
    {
        m_done = xSemaphoreCreateCounting(64, 0);

        if (!m_done)
            return ESP_ERR_NO_MEM;

        // The panel needs its own supply on battery; RD is unused but must idle high.
        gpio_config_t outputs = {};

        outputs.pin_bit_mask = (1ull << pins::lcd_power) | (1ull << pins::lcd_rd);
        outputs.mode = GPIO_MODE_OUTPUT;

        ESP_RETURN_ON_ERROR(gpio_config(&outputs), "lcd", "gpio");
        gpio_set_level(pins::lcd_power, 1);
        gpio_set_level(pins::lcd_rd, 1);

        esp_lcd_i80_bus_config_t bus = {};

        bus.dc_gpio_num = pins::lcd_dc;
        bus.wr_gpio_num = pins::lcd_wr;
        bus.clk_src = LCD_CLK_SRC_DEFAULT;
        bus.data_gpio_nums[0] = pins::lcd_d0;
        bus.data_gpio_nums[1] = pins::lcd_d1;
        bus.data_gpio_nums[2] = pins::lcd_d2;
        bus.data_gpio_nums[3] = pins::lcd_d3;
        bus.data_gpio_nums[4] = pins::lcd_d4;
        bus.data_gpio_nums[5] = pins::lcd_d5;
        bus.data_gpio_nums[6] = pins::lcd_d6;
        bus.data_gpio_nums[7] = pins::lcd_d7;
        bus.bus_width = 8;
        bus.max_transfer_bytes = width * max_transfer_rows * 2;
        bus.dma_burst_size = 64;

        esp_lcd_i80_bus_handle_t i80 = nullptr;

        ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus, &i80), "lcd", "i80 bus");

        // Pixels arrive big-endian already, so bytes go out in memory order.
        esp_lcd_panel_io_i80_config_t io_config = {};

        io_config.cs_gpio_num = pins::lcd_cs;
        io_config.pclk_hz = pixel_clock_hz;
        io_config.trans_queue_depth = 4;
        io_config.on_color_trans_done = on_transfer_done;
        io_config.user_ctx = this;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.dc_levels.dc_idle_level = 0;
        io_config.dc_levels.dc_cmd_level = 0;
        io_config.dc_levels.dc_dummy_level = 0;
        io_config.dc_levels.dc_data_level = 1;

        esp_lcd_panel_io_handle_t io = nullptr;

        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(i80, &io_config, &io), "lcd", "panel io");

        esp_lcd_panel_dev_config_t panel_config = {};

        panel_config.reset_gpio_num = pins::lcd_rst;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;

        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_config, &m_panel), "lcd", "st7789");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(m_panel), "lcd", "reset");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_init(m_panel), "lcd", "init");
        // Landscape: the same settings as the C5 panel (LovyanGFX rotation 1).
        ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(m_panel, true), "lcd", "swap");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(m_panel, true, false), "lcd", "mirror");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(m_panel, true), "lcd", "invert");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(m_panel, 0, 35), "lcd", "gap");

        ledc_timer_config_t timer = {};

        timer.speed_mode = LEDC_LOW_SPEED_MODE;
        timer.duty_resolution = LEDC_TIMER_8_BIT;
        timer.timer_num = backlight_timer;
        timer.freq_hz = 5000;
        timer.clk_cfg = LEDC_AUTO_CLK;

        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), "lcd", "backlight timer");

        ledc_channel_config_t channel = {};

        channel.gpio_num = pins::lcd_backlight;
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = backlight_channel;
        channel.timer_sel = backlight_timer;
        channel.duty = 0;

        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), "lcd", "backlight");

        return esp_lcd_panel_disp_on_off(m_panel, true);
    }

    uint32_t lcd::draw(int x, int y, int w, int h, const uint16_t *pixels)
    {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(m_panel, x, y, x + w, y + h, pixels));

        return ++m_queued;
    }

    void lcd::wait(uint32_t ticket)
    {
        while (static_cast<int32_t>(m_completed.load() - ticket) < 0)
            xSemaphoreTake(m_done, portMAX_DELAY);
    }

    void lcd::set_backlight(uint8_t percent)
    {
        const uint32_t duty = percent >= 100 ? 255 : percent * 255u / 100u;

        ledc_set_duty(LEDC_LOW_SPEED_MODE, backlight_channel, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, backlight_channel);
    }

    bool lcd::on_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *context)
    {
        auto *self = static_cast<lcd *>(context);
        BaseType_t woken = pdFALSE;

        self->m_completed.fetch_add(1);
        xSemaphoreGiveFromISR(self->m_done, &woken);

        return woken == pdTRUE;
    }
}
