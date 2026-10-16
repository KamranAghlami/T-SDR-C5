// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include <esp_err.h>

#include "board/battery.hpp"
#include "board/lcd.hpp"
#include "core/spectrum.hpp"
#include "core/state.hpp"

namespace ui
{
    inline constexpr int width = board::lcd::width;

    // One update from the control loop. When `new_line` is false only the
    // status bar changes (e.g. while frozen).
    struct frame
    {
        std::array<uint8_t, width> peak;
        std::array<uint8_t, width> mean;
        core::state state;
        bool new_line = false;
        bool radio_error = false;
        uint8_t gain = 0; // PHY gain index in use
        uint16_t lines_per_s = 0;
        uint16_t captures = 0;
        board::battery_status battery;
        const char *version = nullptr; // static string
    };

    class renderer
    {
    public:
        esp_err_t init(board::lcd &lcd);

        // Blanks the panel (its RAM holds noise after power-up).
        void clear();

        void show(const frame &f);

        // Average CPU time spent rendering rows per frame (excludes SPI waits).
        uint32_t render_us() const { return m_render_us; }

    private:
        static constexpr int band_rows = 16;
        static constexpr int status_y = 0, status_h = 16;
        static constexpr int spectrum_y = 16, spectrum_h = 40;
        static constexpr int axis_y = 56, axis_h = 12;
        static constexpr int waterfall_y = 68, waterfall_h = board::lcd::height - waterfall_y;
        // Display range: 40 dB (80 codes) above a point just below the noise floor.
        static constexpr int range_codes = 80;
        static constexpr int floor_margin_codes = 4;

        void render_row(int y, uint16_t *row) const;
        void status_row(int y, uint16_t *row) const;
        void spectrum_row(int y, uint16_t *row) const;
        void waterfall_row(int y, uint16_t *row) const;
        void menu_row(int y, uint16_t *row) const;
        void build_axis(const core::state &state);
        void build_channel_axis(unsigned centre_mhz, unsigned span_mhz);
        void build_band_axis(core::band band, unsigned cursor_mhz);
        template <typename RowFn> void flush(RowFn &&row_fn);
        int level(uint8_t code, int scale) const;

        board::lcd *m_lcd = nullptr;
        uint16_t *m_bands[2] = {};
        uint32_t m_tickets[2] = {};
        unsigned m_next_band = 0;
        uint8_t *m_waterfall = nullptr; // waterfall_h lines, ring
        unsigned m_head = 0;
        uint16_t *m_axis = nullptr; // axis_h rows, cached per tuning
        uint32_t m_mapping = 0;     // pixel-to-frequency mapping of the waterfall
        unsigned m_axis_cursor = 0; // selected channel the axis was drawn for
        int m_marker_x = width / 2; // selected channel's centre in the spectrum
        std::array<uint8_t, width> m_mean{};
        std::array<uint8_t, width> m_hold{};
        // Plot heights of m_mean / m_hold, recomputed once per frame.
        std::array<uint8_t, width> m_mean_h{};
        std::array<uint8_t, width> m_hold_h{};
        uint32_t m_render_us = 0;
        core::floor_tracker m_floor;
        frame m_last{};
        char m_status_left[24] = {};
        char m_status_right[2][24] = {};
    };
}
