// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"

#include <algorithm>
#include <cstddef>

#include <nvs.h>

namespace settings
{
    namespace
    {
        constexpr const char *nvs_namespace = "t-sdr-c5";
        constexpr const char *nvs_key = "state";
        constexpr uint8_t layout_version = 4;

        // Stable on-flash layout, independent of core::state. Fields are only
        // ever appended: version 1 ended before `gain`, 2 before `view`, 3 before
        // `frequency`. Versions 1-3 tuned by Wi-Fi channel index.
        struct stored
        {
            uint8_t version;
            uint8_t band;
            uint8_t channel_index[2];
            uint8_t span_mhz;
            uint8_t detector;
            uint8_t colormap;
            uint8_t brightness;
            uint8_t gain;
            uint8_t view;
            uint8_t frequency[2][2]; // MHz per band, little-endian (no padding)
            uint8_t step_mhz;
        };

        constexpr size_t size_of_version(uint8_t version)
        {
            switch (version)
            {
            case 1:
                return offsetof(stored, gain);
            case 2:
                return offsetof(stored, view);
            case 3:
                return offsetof(stored, frequency);
            default:
                return sizeof(stored);
            }
        }
    }

    core::state load()
    {
        core::state state;
        nvs_handle_t handle;

        if (nvs_open(nvs_namespace, NVS_READONLY, &handle) != ESP_OK)
            return state;

        stored s{};
        size_t size = sizeof(s);
        const bool ok = nvs_get_blob(handle, nvs_key, &s, &size) == ESP_OK && s.version >= 1 &&
                        s.version <= layout_version && size == size_of_version(s.version);

        nvs_close(handle);

        if (!ok)
            return state;

        state.band = s.band ? core::band::ghz5 : core::band::ghz24;

        for (unsigned b = 0; b < 2; b++)
        {
            const auto band = static_cast<core::band>(b);
            const auto range = core::range_of(band);
            unsigned mhz = 0;

            if (s.version >= 4)
                mhz = s.frequency[b][0] | (s.frequency[b][1] << 8);
            else if (s.channel_index[b] < core::channels(band).size())
                mhz = core::channels(band)[s.channel_index[b]].mhz;

            if (mhz >= range.low_mhz && mhz <= range.high_mhz)
                state.frequency_mhz[b] = static_cast<uint16_t>(mhz);
        }

        if (std::ranges::find(core::spans_mhz, s.span_mhz) != core::spans_mhz.end())
            state.span_mhz = s.span_mhz;

        state.detector = s.detector ? core::detector::mean : core::detector::peak;
        state.colormap = s.colormap ? core::colormap_id::grayscale : core::colormap_id::turbo;
        state.brightness = std::clamp<uint8_t>(s.brightness, 10, 100);

        if (s.version >= 2)
            state.gain = s.gain;

        if (s.version >= 3)
            state.view = s.view ? core::view_id::band : core::view_id::channel;

        if (s.version >= 4 && std::ranges::find(core::steps_mhz, s.step_mhz) != core::steps_mhz.end())
            state.step_mhz = s.step_mhz;

        return state;
    }

    void save(const core::state &state)
    {
        const stored s = {
            .version = layout_version,
            .band = static_cast<uint8_t>(state.band),
            .channel_index = {0, 0}, // unused since version 4
            .span_mhz = state.span_mhz,
            .detector = static_cast<uint8_t>(state.detector),
            .colormap = static_cast<uint8_t>(state.colormap),
            .brightness = state.brightness,
            .gain = state.gain,
            .view = static_cast<uint8_t>(state.view),
            .frequency = {{static_cast<uint8_t>(state.frequency_mhz[0]), static_cast<uint8_t>(state.frequency_mhz[0] >> 8)},
                          {static_cast<uint8_t>(state.frequency_mhz[1]), static_cast<uint8_t>(state.frequency_mhz[1] >> 8)}},
            .step_mhz = state.step_mhz,
        };
        nvs_handle_t handle;

        if (nvs_open(nvs_namespace, NVS_READWRITE, &handle) != ESP_OK)
            return;

        if (nvs_set_blob(handle, nvs_key, &s, sizeof(s)) == ESP_OK)
            nvs_commit(handle);

        nvs_close(handle);
    }
}
