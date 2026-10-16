// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>

#include <esp_app_desc.h>
#include <esp_cpu.h>
#include <sdkconfig.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "board/battery.hpp"
#include "board/buttons.hpp"
#include "board/lcd.hpp"
#include "c5_radio.h"
#include "core/fft.hpp"
#include "core/gain.hpp"
#include "core/menu.hpp"
#include "core/spectrum.hpp"
#include "core/state.hpp"
#include "settings.hpp"
#include "ui/renderer.hpp"

namespace
{
    constexpr const char *tag = "t-sdr-c5";
    constexpr int64_t line_period_us = 40'000; // 25 waterfall lines per second
    constexpr int64_t idle_refresh_us = 200'000;
    constexpr int64_t save_delay_us = 2'000'000;
    constexpr TickType_t battery_period = pdMS_TO_TICKS(5000);

    board::lcd lcd;
    board::buttons buttons;
    board::battery battery;
    ui::renderer renderer;
    QueueHandle_t gestures;
    QueueHandle_t frames;
    QueueHandle_t battery_status; // length 1, overwritten

    void ui_task(void *)
    {
        static ui::frame f;

        for (;;)
            if (xQueueReceive(frames, &f, portMAX_DELAY) == pdTRUE)
                renderer.show(f);
    }

    void battery_task(void *)
    {
        for (;;)
        {
            const board::battery_status status = battery.read();

            xQueueOverwrite(battery_status, &status);
            vTaskDelay(battery_period);
        }
    }

    struct effects
    {
        bool retune = false;
        bool respan = false;
        bool regain = false;
        bool brightness = false;
        bool save = false;
    };

    effects apply(core::gesture g, core::state &state)
    {
        effects e;

        if (state.menu_open)
        {
            switch (g)
            {
            case core::gesture::short_a:
                state.menu_index = static_cast<uint8_t>((state.menu_index + 1) % core::menu_items.size());
                break;

            case core::gesture::short_b:
            {
                const auto item = core::menu_items[state.menu_index];

                if (core::menu_change(state, item, c5_radio_gain_max()))
                {
                    e.save = true;
                    e.respan = item == core::menu_item::span;
                    e.regain = item == core::menu_item::gain;
                    e.brightness = item == core::menu_item::brightness;
                }
                break;
            }

            case core::gesture::long_a:
            case core::gesture::long_b:
            case core::gesture::both:
                state.menu_open = false;
                break;

            case core::gesture::none:
                break;
            }

            return e;
        }

        auto &index = state.channel_index[static_cast<unsigned>(state.band)];

        switch (g)
        {
        case core::gesture::short_a:
        case core::gesture::short_b:
            index = static_cast<uint8_t>(core::step_channel(state.band, index, g == core::gesture::short_a ? -1 : 1));
            state.frozen = false;
            e.retune = e.save = true;
            break;

        case core::gesture::long_a:
            state.band = core::other(state.band);
            state.frozen = false;
            e.retune = e.save = true;
            break;

        case core::gesture::long_b:
            state.menu_open = true;
            state.menu_index = 0;
            break;

        case core::gesture::both:
            state.frozen = !state.frozen;
            break;

        case core::gesture::none:
            break;
        }

        return e;
    }

    // Applies the gain setting; returns the (clamped) auto-level index.
    unsigned apply_gain(const core::state &state, unsigned auto_index)
    {
        const unsigned max_gain = c5_radio_gain_max();

        auto_index = std::min(auto_index, max_gain);

        if (state.gain == core::gain_agc)
            c5_radio_gain_hardware();
        else if (state.gain == core::gain_auto)
            c5_radio_gain_manual(auto_index);
        else
            c5_radio_gain_manual(std::min<unsigned>(state.gain, max_gain));

        return auto_index;
    }

    void radio_loop(const char *version)
    {
        core::state state = settings::load();
        const core::fft_q15 fft(core::fft_log2);
        const auto window = core::hann_q15(core::fft_size);
        auto *samples = static_cast<int16_t *>(heap_caps_malloc(core::fft_size * 4, MALLOC_CAP_INTERNAL));
        core::line_accumulator accumulator;
        core::auto_level leveller;
        static ui::frame f;

        assert(samples);
        accumulator.configure(core::fft_size, core::span_bins(state.span_mhz));
        lcd.set_backlight(state.brightness);
        f.version = version;

        unsigned auto_gain = c5_radio_gain_max() * 3 / 4;
        bool retune = true;
        int64_t next_line = 0, save_at = 0, rate_start = esp_timer_get_time(), last_frame = 0;
        unsigned lines = 0, line_peak = 0, second_peak = 0;
        // CPU cycles spent per phase during the current second, for the log.
        uint64_t capture_cycles = 0, dsp_cycles = 0;
        unsigned timed = 0;
        uint8_t gain = 0;
        c5_radio_stats_t last_stats;

        c5_radio_get_stats(&last_stats);

        for (;;)
        {
            core::gesture g;

            while (xQueueReceive(gestures, &g, 0) == pdTRUE)
            {
                const effects e = apply(g, state);

                retune |= e.retune;

                if (e.respan)
                    accumulator.configure(core::fft_size, core::span_bins(state.span_mhz));

                if (e.regain)
                {
                    leveller.reset();
                    auto_gain = apply_gain(state, auto_gain);
                }

                if (e.brightness)
                    lcd.set_backlight(state.brightness);

                if (e.save)
                    save_at = esp_timer_get_time() + save_delay_us;

                last_frame = 0; // show the change right away
            }

            if (retune)
            {
                const unsigned mhz = state.current_channel().mhz;

                if (c5_radio_tune(mhz) != ESP_OK)
                    ESP_LOGW(tag, "tune %u MHz failed", mhz);

                // Retuning reprograms the receiver; restore the gain setting.
                leveller.reset();
                auto_gain = apply_gain(state, auto_gain);
                accumulator.reset();
                line_peak = 0;
                retune = false;
                next_line = esp_timer_get_time() + line_period_us;
            }

            int64_t now = esp_timer_get_time();

            if (save_at && now >= save_at)
            {
                settings::save(state);
                save_at = 0;
            }

            xQueuePeek(battery_status, &f.battery, 0);

            if (state.frozen)
            {
                if (now - last_frame >= idle_refresh_us)
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
            const uint32_t t0 = esp_cpu_get_cycle_count();

            if (c5_radio_capture(core::fft_size, C5_RATE_80M, &words, nullptr) == ESP_OK)
            {
                const uint32_t t1 = esp_cpu_get_cycle_count();

                gain = static_cast<uint8_t>((words[0] >> 20) & 0x7f);
                const auto block = core::prepare_iq(words, core::fft_size, window.data(), fft.order(), samples);

                line_peak = std::max(line_peak, block.peak);
                fft.forward_permuted(samples);
                accumulator.add(samples, block.scale);
                capture_cycles += t1 - t0;
                dsp_cycles += esp_cpu_get_cycle_count() - t1;
                timed++;
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

            if (state.gain == core::gain_auto)
            {
                const unsigned next = leveller.update(auto_gain, c5_radio_gain_max(), line_peak);

                if (next != auto_gain)
                {
                    auto_gain = next;
                    c5_radio_gain_manual(auto_gain);
                }
            }

            second_peak = std::max(second_peak, line_peak);
            line_peak = 0;

            if (now - rate_start >= 1'000'000)
            {
                c5_radio_stats_t stats;

                c5_radio_get_stats(&stats);
                f.lines_per_s = static_cast<uint16_t>(lines);
                constexpr unsigned mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
                const auto per_capture_us = [&](uint64_t cycles) {
                    return static_cast<unsigned>(timed ? cycles / timed / mhz : 0);
                };

                ESP_LOGI(tag,
                         "%u lines/s, %u captures/line (capture %u us, dsp %u us, ui %u us/frame), %u failures, "
                         "%u recalibrations, gain %u, peak %u, SRAM %u, battery %u mV %d mA",
                         lines, f.captures, per_capture_us(capture_cycles), per_capture_us(dsp_cycles),
                         static_cast<unsigned>(renderer.render_us()),
                         static_cast<unsigned>(stats.failures - last_stats.failures),
                         static_cast<unsigned>(stats.recalibrations - last_stats.recalibrations), gain, second_peak,
                         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), f.battery.millivolts,
                         f.battery.milliamps);
                capture_cycles = dsp_cycles = 0;
                timed = 0;
                last_stats = stats;
                lines = 0;
                second_peak = 0;
                rate_start = now;
            }

            next_line += line_period_us;

            if (next_line < now)
                next_line = now + line_period_us;

            // Let lower-priority tasks (idle, logging, battery) run once per line.
            vTaskDelay(1);
        }
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(lcd.init());
    ESP_ERROR_CHECK(renderer.init(lcd));
    renderer.clear();

    const char *version = esp_app_get_description()->version;

    ESP_LOGI(tag, "t-sdr-c5 %s", version);
    // The vendor PHY code logs per capture/retune; that also costs time.
    esp_log_level_set("phy", ESP_LOG_WARN);

    gestures = xQueueCreate(8, sizeof(core::gesture));
    frames = xQueueCreate(2, sizeof(ui::frame));
    battery_status = xQueueCreate(1, sizeof(board::battery_status));
    ESP_ERROR_CHECK(buttons.init(gestures));

    const board::battery_status none;

    xQueueOverwrite(battery_status, &none);

    if (battery.init() == ESP_OK)
        xTaskCreate(battery_task, "battery", 3072, nullptr, 2, nullptr);
    else
        ESP_LOGW(tag, "no AXP2602 fuel gauge found");

    // Also initialises NVS, which settings::load() needs.
    ESP_ERROR_CHECK(c5_radio_init());
    ESP_ERROR_CHECK(c5_radio_bandwidth(0));

    // The UI blocks on SPI most of the time; above the radio loop it can
    // refill the next display band as soon as a transfer finishes.
    xTaskCreate(ui_task, "ui", 6144, nullptr, 5, nullptr);
    vTaskPrioritySet(nullptr, 4);
    radio_loop(version);
}
