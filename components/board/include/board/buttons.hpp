// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "core/gestures.hpp"

namespace board
{
    // Samples both buttons every 10 ms and posts core::gesture values to a queue.
    class buttons
    {
    public:
        esp_err_t init(QueueHandle_t gestures);

    private:
        static void on_tick(void *context);

        QueueHandle_t m_gestures = nullptr;
        esp_timer_handle_t m_timer = nullptr;
        core::gesture_decoder m_decoder;
    };
}
