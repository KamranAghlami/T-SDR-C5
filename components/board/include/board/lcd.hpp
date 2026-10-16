// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include <esp_err.h>
#include <esp_lcd_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace board
{
    // ST7789 170x320 IPS panel, used in landscape as 320x170.
    class lcd
    {
    public:
        static constexpr int width = 320;
        static constexpr int height = 170;

        esp_err_t init();

        // Queues a window of big-endian RGB565 pixels. The buffer must stay
        // untouched until wait(ticket) returns. May block while an earlier
        // transfer is still finishing (the panel IO serializes windows).
        uint32_t draw(int x, int y, int w, int h, const uint16_t *pixels);

        void wait(uint32_t ticket);

        void set_backlight(uint8_t percent);

    private:
        static bool on_transfer_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context);

        esp_lcd_panel_handle_t m_panel = nullptr;
        SemaphoreHandle_t m_done = nullptr;
        uint32_t m_queued = 0;
        std::atomic<uint32_t> m_completed = 0;
    };
}
