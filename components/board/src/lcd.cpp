// SPDX-License-Identifier: GPL-3.0-or-later
#include "board/lcd.hpp"

#include <driver/ledc.h>
#include <esp_check.h>
#include <driver/spi_master.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>

#include "board/pins.hpp"

namespace board
{
    namespace
    {
        constexpr unsigned pixel_clock_hz = 40'000'000;
        constexpr int max_transfer_rows = 16;
        constexpr ledc_channel_t backlight_channel = LEDC_CHANNEL_0;
        constexpr ledc_timer_t backlight_timer = LEDC_TIMER_0;
    }

    esp_err_t lcd::init()
    {
        m_done = xSemaphoreCreateCounting(64, 0);

        if (!m_done)
            return ESP_ERR_NO_MEM;

        // Unused data lines must be -1, not 0: GPIO0 is button A.
        spi_bus_config_t bus = {};

        bus.mosi_io_num = pins::lcd_mosi;
        bus.miso_io_num = -1;
        bus.sclk_io_num = pins::lcd_sck;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.data4_io_num = -1;
        bus.data5_io_num = -1;
        bus.data6_io_num = -1;
        bus.data7_io_num = -1;
        bus.max_transfer_sz = width * max_transfer_rows * 2;

        ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), "lcd", "spi bus");

        esp_lcd_panel_io_spi_config_t io_config = {};

        io_config.cs_gpio_num = pins::lcd_cs;
        io_config.dc_gpio_num = pins::lcd_dc;
        io_config.spi_mode = 0;
        io_config.pclk_hz = pixel_clock_hz;
        io_config.trans_queue_depth = 4;
        io_config.on_color_trans_done = on_transfer_done;
        io_config.user_ctx = this;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;

        esp_lcd_panel_io_handle_t io = nullptr;

        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config, &io),
                            "lcd", "panel io");

        esp_lcd_panel_dev_config_t panel_config = {};

        panel_config.reset_gpio_num = pins::lcd_rst;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; // verified on hardware
        panel_config.bits_per_pixel = 16;

        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_config, &m_panel), "lcd", "st7789");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(m_panel), "lcd", "reset");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_init(m_panel), "lcd", "init");
        // Landscape, as in the LilyGO factory firmware.
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
