// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "core/colormap.hpp"
#include "core/font5x7.hpp"
#include "core/menu.hpp"
#include "core/survey.hpp"

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
        constexpr uint16_t menu_bg = core::rgb565(10, 14, 24);
        constexpr uint16_t menu_selected = core::rgb565(0, 70, 120);
        constexpr int menu_row_h = 22;
        // Axis rows: cursor bar 0-1, labels 2-8, ticks 10-11 (pointing at the waterfall).
        constexpr int label_row = 2;
        constexpr int tick_row = 10;

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
        const bool band_view = f.state.view == core::view_id::band;
        // What each pixel means. In the band view the cursor moves without
        // changing it, so the waterfall survives cursor moves.
        const uint32_t mapping = band_view ? 0x80000000u | static_cast<uint32_t>(f.state.band)
                                           : (static_cast<uint32_t>(channel.mhz) << 8) | f.state.span_mhz;

        if (mapping != m_mapping)
        {
            m_mapping = mapping;
            m_hold.fill(0);
            m_floor.reset();
            std::memset(m_waterfall, 0, waterfall_h * width);
            m_axis_cursor = 0; // force an axis rebuild
        }

        if (channel.mhz != m_axis_cursor)
        {
            m_axis_cursor = channel.mhz;
            build_axis(f.state);
        }

        std::snprintf(m_status_left, sizeof(m_status_left), "%s CH%u %u", core::band_name(f.state.band), channel.number,
                      channel.mhz);
        const char *mode = f.state.frozen ? "FROZEN" : f.state.detector == core::detector::peak ? "PEAK" : "MEAN";

        if (band_view)
            std::snprintf(m_status_right[0], sizeof(m_status_right[0]), "BAND %s", mode);
        else
            std::snprintf(m_status_right[0], sizeof(m_status_right[0]), "%uMHz %s", f.state.span_mhz, mode);
        const char gain_mode = f.state.gain == core::gain_auto ? 'A' : f.state.gain == core::gain_agc ? 'H' : 'G';
        char battery[8] = "";

        switch (f.battery.source)
        {
        case board::power_source::charging:
        case board::power_source::discharging:
            std::snprintf(battery, sizeof(battery), " %u%%%s", f.battery.percent,
                          f.battery.source == board::power_source::charging ? "+" : "");
            break;

        case board::power_source::external:
            std::snprintf(battery, sizeof(battery), " USB");
            break;

        case board::power_source::unknown:
            break;
        }

        std::snprintf(m_status_right[1], sizeof(m_status_right[1]), "%u/s %c%u%s", f.lines_per_s, gain_mode, f.gain,
                      battery);

        for (int x = 0; x < width; x++)
        {
            m_mean_h[x] = static_cast<uint8_t>(level(m_mean[x], spectrum_h));
            m_hold_h[x] = static_cast<uint8_t>(level(m_hold[x], spectrum_h));
        }

        m_last = f;

        int64_t busy = 0;

        flush([this, &busy](int y, uint16_t *row) {
            const int64_t start = esp_timer_get_time();

            render_row(y, row);
            busy += esp_timer_get_time() - start;
        });

        // Smoothed over about 8 frames.
        m_render_us = static_cast<uint32_t>((m_render_us * 7 + static_cast<uint32_t>(busy)) / 8);
    }

    void renderer::render_row(int y, uint16_t *row) const
    {
        if (y < spectrum_y)
            status_row(y, row);
        else if (m_last.state.menu_open)
            menu_row(y - spectrum_y, row);
        else if (y < axis_y)
            spectrum_row(y - spectrum_y, row);
        else if (y < waterfall_y)
            std::memcpy(row, m_axis + (y - axis_y) * width, width * 2);
        else
            waterfall_row(y - waterfall_y, row);
    }

    // Menu over the spectrum and waterfall: one item per row, value right-aligned.
    void renderer::menu_row(int y, uint16_t *row) const
    {
        const int index = y / menu_row_h;
        const bool selected = index == m_last.state.menu_index;

        std::fill(row, row + width, selected ? menu_selected : menu_bg);

        if (index >= static_cast<int>(core::menu_items.size()))
            return;

        const auto item = core::menu_items[index];
        char value[24];

        core::menu_value(m_last.state, item, value, sizeof(value), m_last.version);
        text_row(row, y % menu_row_h - 4, 6, core::menu_label(item), 2, selected ? white : grey);
        // Long values (the version string) drop to the small font.
        const int scale = text_width(value, 2) > width - 96 ? 1 : 2;

        text_row(row, y % menu_row_h - (scale == 2 ? 4 : 7), width - 6 - text_width(value, scale), value, scale, white);
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
            const int mean = m_mean_h[x];
            const int peak = m_hold_h[x];
            uint16_t c = x == m_marker_x ? centre : bg;

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

    void renderer::build_axis(const core::state &state)
    {
        const auto &channel = state.current_channel();

        if (state.view == core::view_id::band)
            build_band_axis(state.band, channel.mhz);
        else
            build_channel_axis(channel.mhz, state.span_mhz);
    }

    void renderer::build_channel_axis(unsigned centre, unsigned span_mhz)
    {
        const int span = static_cast<int>(span_mhz);
        const int centre_mhz = static_cast<int>(centre);
        const int label_step = span <= 20 ? 5 : span <= 40 ? 10 : 20;
        const auto x_of = [&](int mhz) { return width / 2 + (mhz - centre_mhz) * width / span; };
        char label[12];

        m_marker_x = width / 2;

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

                if (r >= tick_row)
                    row[x] = grey;

                std::snprintf(label, sizeof(label), "%d", mhz);

                const int lx = std::clamp(x - text_width(label, 1) / 2, 0, width - text_width(label, 1));

                text_row(row, r - label_row, lx, label, 1, grey);
            }
        }
    }

    // Channel numbers across the whole band, the cursor channel highlighted.
    // 5 GHz channels sit 9 px apart, so labels skip channels that would overlap.
    void renderer::build_band_axis(core::band band, unsigned cursor_mhz)
    {
        struct label
        {
            int x, w;
            char text[8];
            bool cursor;
        };

        const auto plan = core::make_survey_plan(band);
        const int span = plan.high_mhz - plan.low_mhz;
        const auto x_of = [&](int mhz) { return (mhz - plan.low_mhz) * width / span; };
        const auto channels = core::channels(band);
        std::array<label, core::channels_5.size()> labels{};
        size_t count = 0;

        for (const auto &c : channels)
        {
            label &l = labels[count++];

            std::snprintf(l.text, sizeof(l.text), "%u", c.number);
            l.w = text_width(l.text, 1);
            l.x = std::clamp(x_of(c.mhz) - l.w / 2, 0, width - l.w);
            l.cursor = c.mhz == cursor_mhz;
        }

        // Keep the cursor's label, then every other one that clears its neighbours.
        const auto cursor = std::ranges::find_if(labels.begin(), labels.begin() + count, &label::cursor);
        const auto overlaps = [](const label &a, const label &b) { return a.x < b.x + b.w + 3 && b.x < a.x + a.w + 3; };
        std::array<bool, core::channels_5.size()> shown{};
        const label *previous = nullptr;

        for (size_t i = 0; i < count; i++)
        {
            const label &l = labels[i];

            shown[i] = l.cursor || ((!previous || !overlaps(*previous, l)) &&
                                    (cursor == labels.begin() + count || !overlaps(*cursor, l)));

            if (shown[i])
                previous = &l;
        }

        m_marker_x = x_of(static_cast<int>(cursor_mhz));

        for (int r = 0; r < axis_h; r++)
        {
            uint16_t *row = m_axis + r * width;

            std::fill(row, row + width, axis_bg);

            if (r < 2)
                std::fill(row + std::max(0, x_of(static_cast<int>(cursor_mhz) - 10)),
                          row + std::min(width, x_of(static_cast<int>(cursor_mhz) + 10)), channel_bar);

            for (size_t i = 0; i < count; i++)
            {
                const int x = x_of(channels[i].mhz);

                if (r >= tick_row && x >= 0 && x < width)
                    row[x] = labels[i].cursor ? white : grey;

                if (shown[i])
                    text_row(row, r - label_row, labels[i].x, labels[i].text, 1, labels[i].cursor ? white : grey);
            }
        }
    }
}
