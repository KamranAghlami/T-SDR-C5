// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>

#include "core/colormap.hpp"
#include "core/font5x7.hpp"

namespace ui
{
    namespace
    {
        constexpr uint16_t black = core::rgb565(0, 0, 0);
        constexpr uint16_t white = core::rgb565(255, 255, 255);
        constexpr uint16_t grey = core::rgb565(140, 140, 140);
        constexpr uint16_t status_bg = core::rgb565(0, 28, 64);
        constexpr uint16_t frozen_bg = core::rgb565(90, 40, 0);
        constexpr uint16_t error_bg = core::rgb565(120, 0, 0);
        constexpr uint16_t grid = core::rgb565(36, 36, 36);
        constexpr uint16_t centre = core::rgb565(70, 20, 20);
        constexpr uint16_t fill = core::rgb565(0, 80, 100);
        constexpr uint16_t trace = core::rgb565(0, 230, 255);
        constexpr uint16_t hold = core::rgb565(255, 210, 0);
        constexpr uint16_t axis_bg = core::rgb565(18, 18, 18);
        constexpr uint16_t channel_bar = core::rgb565(0, 160, 200);

        // Draws row `gy` (0 = top) of `text` at scale `scale` into a pixel row.
        void text_row(uint16_t *row, int gy, int x, const char *text, int scale, uint16_t colour)
        {
            if (gy < 0 || gy >= 7 * scale)
                return;

            gy /= scale;

            for (; *text; text++, x += 6 * scale)
            {
                const unsigned c = static_cast<unsigned char>(*text);
                const uint8_t *glyph = core::font5x7[(c < 32 || c > 126 ? '?' : c) - 32];

                for (int col = 0; col < 5; col++)
                {
                    if (!((glyph[col] >> gy) & 1))
                        continue;

                    for (int dx = 0; dx < scale; dx++)
                    {
                        const int px = x + col * scale + dx;

                        if (px >= 0 && px < width)
                            row[px] = colour;
                    }
                }
            }
        }

        int text_width(const char *text, int scale) { return static_cast<int>(std::strlen(text)) * 6 * scale - scale; }

        const std::array<uint16_t, 256> &palette(core::colormap_id id)
        {
            return id == core::colormap_id::grayscale ? core::grayscale : core::turbo;
        }
    }

    esp_err_t renderer::init(board::lcd &lcd)
    {
        m_lcd = &lcd;

        for (auto &band : m_bands)
        {
            band = static_cast<uint16_t *>(heap_caps_malloc(width * band_rows * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

            if (!band)
                return ESP_ERR_NO_MEM;
        }

        m_waterfall = static_cast<uint8_t *>(heap_caps_calloc(waterfall_h, width, MALLOC_CAP_SPIRAM));
        m_axis = static_cast<uint16_t *>(heap_caps_calloc(axis_h * width, 2, MALLOC_CAP_SPIRAM));

        return m_waterfall && m_axis ? ESP_OK : ESP_ERR_NO_MEM;
    }

    template <typename RowFn> void renderer::flush(RowFn &&row_fn)
    {
        for (int y = 0; y < board::lcd::height; y += band_rows)
        {
            const int rows = std::min(band_rows, board::lcd::height - y);
            const unsigned b = m_next_band;

            m_next_band ^= 1;
            m_lcd->wait(m_tickets[b]);

            for (int r = 0; r < rows; r++)
                row_fn(y + r, m_bands[b] + r * width);

            m_tickets[b] = m_lcd->draw(0, y, width, rows, m_bands[b]);
        }
    }

    void renderer::clear()
    {
        flush([](int, uint16_t *row) { std::fill(row, row + width, black); });
    }

    int renderer::level(uint8_t code, int scale) const
    {
        const int base = static_cast<int>(m_floor.floor()) - floor_margin_codes;

        return std::clamp((static_cast<int>(code) - base) * scale / range_codes, 0, scale);
    }

    void renderer::show(const frame &f)
    {
        if (f.new_line)
        {
            m_floor.update(f.mean.data(), width);
            m_mean = f.mean;

            const auto &codes = f.state.detector == core::detector::peak ? f.peak : f.mean;

            m_head = (m_head + 1) % waterfall_h;

            uint8_t *line = m_waterfall + m_head * width;

            for (int x = 0; x < width; x++)
            {
                line[x] = static_cast<uint8_t>(level(codes[x], 255));
                // Peak hold decays by 0.5 dB per line.
                m_hold[x] = std::max<uint8_t>(m_hold[x] > 0 ? m_hold[x] - 1 : 0, f.peak[x]);
            }
        }

        const auto &channel = f.state.current_channel();

        if (static_cast<unsigned>(channel.mhz) != m_axis_mhz || static_cast<unsigned>(f.state.span_mhz) != m_axis_span)
        {
            m_axis_mhz = channel.mhz;
            m_axis_span = f.state.span_mhz;
            m_hold.fill(0);
            m_floor.reset();
            std::memset(m_waterfall, 0, waterfall_h * width);
            build_axis();
        }

        std::snprintf(m_status_left, sizeof(m_status_left), "%s CH%u %u", core::band_name(f.state.band), channel.number,
                      channel.mhz);
        std::snprintf(m_status_right[0], sizeof(m_status_right[0]), "%uMHz %s", f.state.span_mhz,
                      f.state.frozen ? "FROZEN" : f.state.detector == core::detector::peak ? "PEAK" : "MEAN");
        std::snprintf(m_status_right[1], sizeof(m_status_right[1]), "%u/s G%u", f.lines_per_s, f.gain);

        m_last = f;
        flush([this](int y, uint16_t *row) { render_row(y, row); });
    }

    void renderer::render_row(int y, uint16_t *row) const
    {
        if (y < spectrum_y)
            status_row(y, row);
        else if (y < axis_y)
            spectrum_row(y - spectrum_y, row);
        else if (y < waterfall_y)
            std::memcpy(row, m_axis + (y - axis_y) * width, width * 2);
        else
            waterfall_row(y - waterfall_y, row);
    }

    void renderer::status_row(int y, uint16_t *row) const
    {
        const uint16_t bg = m_last.radio_error ? error_bg : m_last.state.frozen ? frozen_bg : status_bg;

        std::fill(row, row + width, bg);
        text_row(row, y - 1, 4, m_status_left, 2, white);

        for (int line = 0; line < 2; line++)
        {
            const char *text = m_status_right[line];

            text_row(row, y - 1 - 8 * line, width - 3 - text_width(text, 1), text, 1, line ? grey : white);
        }
    }

    void renderer::spectrum_row(int y, uint16_t *row) const
    {
        // Height above the bottom of the plot, 0..spectrum_h - 1.
        const int h = spectrum_h - 1 - y;
        const uint16_t bg = h % 10 == 0 ? grid : black;

        for (int x = 0; x < width; x++)
        {
            const int mean = level(m_mean[x], spectrum_h);
            const int peak = level(m_hold[x], spectrum_h);
            uint16_t c = x == width / 2 ? centre : bg;

            if (h < mean)
                c = h == mean - 1 ? trace : fill;

            if (h == peak - 1 && peak > mean)
                c = hold;

            row[x] = c;
        }
    }

    void renderer::waterfall_row(int y, uint16_t *row) const
    {
        const auto &lut = palette(m_last.state.colormap);
        const uint8_t *line = m_waterfall + ((m_head + waterfall_h - y) % waterfall_h) * width;

        for (int x = 0; x < width; x++)
            row[x] = lut[line[x]];
    }

    void renderer::build_axis()
    {
        const int span = static_cast<int>(m_axis_span);
        const int centre_mhz = static_cast<int>(m_axis_mhz);
        const int label_step = span <= 20 ? 5 : span <= 40 ? 10 : 20;
        const auto x_of = [&](int mhz) { return width / 2 + (mhz - centre_mhz) * width / span; };
        char label[12];

        for (int r = 0; r < axis_h; r++)
        {
            uint16_t *row = m_axis + r * width;

            std::fill(row, row + width, axis_bg);

            // The selected 20 MHz channel.
            if (r < 2)
                std::fill(row + std::max(0, x_of(centre_mhz - 10)), row + std::min(width, x_of(centre_mhz + 10)), channel_bar);

            for (int mhz = (centre_mhz - span / 2) / label_step * label_step; mhz <= centre_mhz + span / 2; mhz += label_step)
            {
                const int x = x_of(mhz);

                if (x < 0 || x >= width)
                    continue;

                if (r >= 2 && r < 4)
                    row[x] = grey;

                std::snprintf(label, sizeof(label), "%d", mhz);

                const int lx = std::clamp(x - text_width(label, 1) / 2, 0, width - text_width(label, 1));

                text_row(row, r - 4, lx, label, 1, grey);
            }
        }
    }
}
