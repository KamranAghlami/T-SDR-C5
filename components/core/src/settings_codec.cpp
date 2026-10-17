// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/settings_codec.hpp"

#include <algorithm>

namespace core::settings_codec
{
    namespace
    {
        enum offset : size_t
        {
            at_version = 0,
            at_band = 1,
            at_channel_index = 2,
            at_span = 4,
            at_detector = 5,
            at_colormap = 6,
            at_brightness = 7,
            at_gain = 8,
            at_view = 9,
            at_frequency = 10,
            at_step = 14,
        };

        constexpr size_t size_of(uint8_t v)
        {
            switch (v)
            {
            case 1:
                return at_gain;
            case 2:
                return at_view;
            case 3:
                return at_frequency;
            case 4:
                return size;
            default:
                return 0;
            }
        }

        template <typename T, size_t N> bool contains(const std::array<T, N> &values, T value)
        {
            return std::ranges::find(values, value) != values.end();
        }
    }

    std::array<uint8_t, size> encode(const state &s)
    {
        std::array<uint8_t, size> blob{};

        blob[at_version] = version;
        blob[at_band] = static_cast<uint8_t>(s.band);
        blob[at_span] = s.span_mhz;
        blob[at_detector] = static_cast<uint8_t>(s.detector);
        blob[at_colormap] = static_cast<uint8_t>(s.colormap);
        blob[at_brightness] = s.brightness;
        blob[at_gain] = s.gain;
        blob[at_view] = static_cast<uint8_t>(s.view);

        for (unsigned b = 0; b < 2; b++)
        {
            blob[at_frequency + 2 * b] = static_cast<uint8_t>(s.frequency_mhz[b]);
            blob[at_frequency + 2 * b + 1] = static_cast<uint8_t>(s.frequency_mhz[b] >> 8);
        }

        blob[at_step] = s.step_mhz;

        return blob;
    }

    std::optional<state> decode(std::span<const uint8_t> blob)
    {
        if (blob.empty() || !size_of(blob[at_version]) || blob.size() != size_of(blob[at_version]))
            return std::nullopt;

        const uint8_t v = blob[at_version];
        state s;

        s.band = blob[at_band] ? band::ghz5 : band::ghz24;

        for (unsigned b = 0; b < 2; b++)
        {
            const auto this_band = static_cast<band>(b);
            const auto range = range_of(this_band);
            const uint8_t index = blob[at_channel_index + b];
            unsigned mhz = 0;

            if (v >= 4)
                mhz = blob[at_frequency + 2 * b] | (blob[at_frequency + 2 * b + 1] << 8);
            else if (index < channels(this_band).size())
                mhz = channels(this_band)[index].mhz;

            if (mhz >= range.low_mhz && mhz <= range.high_mhz)
                s.frequency_mhz[b] = static_cast<uint16_t>(mhz);
        }

        if (contains(spans_mhz, blob[at_span]))
            s.span_mhz = blob[at_span];

        s.detector = blob[at_detector] ? detector::mean : detector::peak;
        s.colormap = blob[at_colormap] ? colormap_id::grayscale : colormap_id::turbo;
        s.brightness = std::clamp<uint8_t>(blob[at_brightness], 10, 100);

        if (v >= 2)
            s.gain = blob[at_gain];

        if (v >= 3)
            s.view = blob[at_view] ? view_id::band : view_id::channel;

        if (v >= 4 && contains(steps_mhz, blob[at_step]))
            s.step_mhz = blob[at_step];

        return s;
    }
}
