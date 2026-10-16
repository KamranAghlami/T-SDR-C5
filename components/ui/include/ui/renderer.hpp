// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include <esp_err.h>

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
        uint8_t gain = 0;
        uint16_t lines_per_s = 0;
        uint16_t captures = 0;
    };

    class renderer
    {
    public:
        esp_err_t init(board::lcd &lcd);

        // Blanks the panel (its RAM holds noise after power-up).
        void clear();

        void show(const frame &f);

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
        void build_axis();
        template <typename RowFn> void flush(RowFn &&row_fn);
        int level(uint8_t code, int scale) const;

        board::lcd *m_lcd = nullptr;
        uint16_t *m_bands[2] = {};
        uint32_t m_tickets[2] = {};
        unsigned m_next_band = 0;
        uint8_t *m_waterfall = nullptr; // waterfall_h lines, ring
        unsigned m_head = 0;
        uint16_t *m_axis = nullptr; // axis_h rows, cached per tuning
        unsigned m_axis_mhz = 0, m_axis_span = 0;
        std::array<uint8_t, width> m_mean{};
        std::array<uint8_t, width> m_hold{};
        core::floor_tracker m_floor;
        frame m_last{};
        char m_status_left[24] = {};
        char m_status_right[2][16] = {};
    };
}
