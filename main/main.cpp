// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "board/buttons.hpp"
#include "board/lcd.hpp"
#include "c5_radio.h"
#include "core/fft.hpp"
#include "core/spectrum.hpp"
#include "core/state.hpp"
#include "settings.hpp"
#include "ui/renderer.hpp"

namespace
{
    constexpr const char *tag = "t-sdr-c5";
    constexpr int64_t line_period_us = 40'000; // 25 waterfall lines per second
    constexpr int64_t frozen_refresh_us = 200'000;
    constexpr int64_t save_delay_us = 2'000'000;

    board::lcd lcd;
    board::buttons buttons;
    ui::renderer renderer;
    QueueHandle_t gestures;
    QueueHandle_t frames;

    void ui_task(void *)
    {
        static ui::frame f;

        for (;;)
            if (xQueueReceive(frames, &f, portMAX_DELAY) == pdTRUE)
                renderer.show(f);
    }

    struct control
    {
        bool retune = false;
        bool respan = false;
        bool changed = false;
    };

    control apply(core::gesture g, core::state &state)
    {
        control c;
        auto &index = state.channel_index[static_cast<unsigned>(state.band)];

        switch (g)
        {
        case core::gesture::short_a:
        case core::gesture::short_b:
            index = static_cast<uint8_t>(core::step_channel(state.band, index, g == core::gesture::short_a ? -1 : 1));
            state.frozen = false;
            c.retune = true;
            break;

        case core::gesture::long_a:
            state.band = core::other(state.band);
            state.frozen = false;
            c.retune = true;
            break;

        case core::gesture::long_b:
        {
            // Until the settings menu lands (M3): cycle the span.
            auto it = std::ranges::find(core::spans_mhz, state.span_mhz);

            state.span_mhz = (it == core::spans_mhz.end() || ++it == core::spans_mhz.end()) ? core::spans_mhz[0] : *it;
            c.respan = true;
            break;
        }

        case core::gesture::both:
            state.frozen = !state.frozen;
            break;

        case core::gesture::none:
            return c;
        }

        c.changed = true;

        return c;
    }

    void radio_loop()
    {
        core::state state = settings::load();
        const core::fft_q15 fft(core::fft_log2);
        const auto window = core::hann_q15(core::fft_size);
        auto *samples = static_cast<int16_t *>(heap_caps_malloc(core::fft_size * 4, MALLOC_CAP_INTERNAL));
        core::line_accumulator accumulator;
        static ui::frame f;

        assert(samples);
        accumulator.configure(core::fft_size, core::span_bins(state.span_mhz));
        lcd.set_backlight(state.brightness);

        bool retune = true;
        int64_t next_line = 0, save_at = 0, rate_start = esp_timer_get_time(), last_frame = 0;
        unsigned lines = 0, errors = 0;
        uint8_t gain = 0;

        for (;;)
        {
            core::gesture g;

            while (xQueueReceive(gestures, &g, 0) == pdTRUE)
            {
                const control c = apply(g, state);

                retune |= c.retune;

                if (c.respan)
                    accumulator.configure(core::fft_size, core::span_bins(state.span_mhz));

                if (c.changed)
                {
                    save_at = esp_timer_get_time() + save_delay_us;
                    last_frame = 0; // show the change right away
                }
            }

            if (retune)
            {
                const unsigned mhz = state.current_channel().mhz;

                if (c5_radio_tune(mhz) != ESP_OK)
                    ESP_LOGW(tag, "tune %u MHz failed", mhz);

                accumulator.reset();
                retune = false;
                next_line = esp_timer_get_time() + line_period_us;
            }

            int64_t now = esp_timer_get_time();

            if (save_at && now >= save_at)
            {
                settings::save(state);
                save_at = 0;
            }

            if (state.frozen)
            {
                if (now - last_frame >= frozen_refresh_us)
                {
                    f.state = state;
                    f.new_line = false;
                    xQueueSend(frames, &f, 0);
                    last_frame = now;
                }

                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            const uint32_t *words;

            if (c5_radio_capture(core::fft_size, C5_RATE_80M, &words, nullptr) == ESP_OK)
            {
                gain = static_cast<uint8_t>((words[0] >> 20) & 0x7f);
                core::prepare_iq(words, core::fft_size, window.data(), samples);
                fft.forward(samples);
                accumulator.add(samples);
            }
            else
            {
                errors++;
            }

            now = esp_timer_get_time();

            if (now < next_line)
                continue;

            f.captures = static_cast<uint16_t>(accumulator.count());
            f.radio_error = f.captures == 0;
            accumulator.finish(f.peak.data(), f.mean.data(), ui::width);
            f.state = state;
            f.new_line = true;
            f.gain = gain;
            xQueueSend(frames, &f, 0); // drop the line if the display is behind
            last_frame = now;
            lines++;

            if (now - rate_start >= 1'000'000)
            {
                f.lines_per_s = static_cast<uint16_t>(lines);
                ESP_LOGI(tag, "%u lines/s, %u captures/line, %u errors, gain %u, free SRAM %u", lines, f.captures, errors,
                         gain, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
                lines = 0;
                errors = 0;
                rate_start = now;
            }

            next_line += line_period_us;

            if (next_line < now)
                next_line = now + line_period_us;

            // Let lower-priority tasks (idle, logging) run once per line.
            vTaskDelay(1);
        }
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(lcd.init());
    ESP_ERROR_CHECK(renderer.init(lcd));

    const char *version = esp_app_get_description()->version;

    renderer.splash(version);
    lcd.set_backlight(80);
    ESP_LOGI(tag, "t-sdr-c5 %s", version);

    gestures = xQueueCreate(8, sizeof(core::gesture));
    frames = xQueueCreate(2, sizeof(ui::frame));
    ESP_ERROR_CHECK(buttons.init(gestures));

    // Also initialises NVS, which settings::load() needs.
    ESP_ERROR_CHECK(c5_radio_init());
    ESP_ERROR_CHECK(c5_radio_bandwidth(0));

    // The UI blocks on SPI most of the time; above the radio loop it can
    // refill the next display band as soon as a transfer finishes.
    xTaskCreate(ui_task, "ui", 6144, nullptr, 5, nullptr);
    vTaskPrioritySet(nullptr, 4);
    radio_loop();
}
