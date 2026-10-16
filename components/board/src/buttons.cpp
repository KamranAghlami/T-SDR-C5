// SPDX-License-Identifier: GPL-3.0-or-later
#include "board/buttons.hpp"

#include <driver/gpio.h>
#include <esp_check.h>

#include "board/pins.hpp"

namespace board
{
    esp_err_t buttons::init(QueueHandle_t gestures)
    {
        m_gestures = gestures;

        const gpio_config_t config = {
            .pin_bit_mask = (1ull << pins::button_a) | (1ull << pins::button_b),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_RETURN_ON_ERROR(gpio_config(&config), "buttons", "gpio");

        const esp_timer_create_args_t timer = {
            .callback = on_tick,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "buttons",
            .skip_unhandled_events = true,
        };

        ESP_RETURN_ON_ERROR(esp_timer_create(&timer, &m_timer), "buttons", "timer");

        return esp_timer_start_periodic(m_timer, 10'000);
    }

    void buttons::on_tick(void *context)
    {
        auto *self = static_cast<buttons *>(context);
        const auto now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        const bool a = !gpio_get_level(pins::button_a);
        const bool b = !gpio_get_level(pins::button_b);
        const core::gesture g = self->m_decoder.update(now_ms, a, b);

        if (g != core::gesture::none)
            xQueueSend(self->m_gestures, &g, 0);
    }
}
