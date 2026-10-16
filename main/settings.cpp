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
        constexpr uint8_t layout_version = 3;

        // Stable on-flash layout, independent of core::state. Fields are only
        // ever appended: version 1 ended before `gain`, version 2 before `view`.
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
        };

        constexpr size_t size_of_version(uint8_t version)
        {
            return version == 1 ? offsetof(stored, gain) : version == 2 ? offsetof(stored, view) : sizeof(stored);
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
            if (s.channel_index[b] < core::channels(static_cast<core::band>(b)).size())
                state.channel_index[b] = s.channel_index[b];

        if (std::ranges::find(core::spans_mhz, s.span_mhz) != core::spans_mhz.end())
            state.span_mhz = s.span_mhz;

        state.detector = s.detector ? core::detector::mean : core::detector::peak;
        state.colormap = s.colormap ? core::colormap_id::grayscale : core::colormap_id::turbo;
        state.brightness = std::clamp<uint8_t>(s.brightness, 10, 100);

        if (s.version >= 2)
            state.gain = s.gain;

        if (s.version >= 3)
            state.view = s.view ? core::view_id::band : core::view_id::channel;

        return state;
    }

    void save(const core::state &state)
    {
        const stored s = {
            .version = layout_version,
            .band = static_cast<uint8_t>(state.band),
            .channel_index = {state.channel_index[0], state.channel_index[1]},
            .span_mhz = state.span_mhz,
            .detector = static_cast<uint8_t>(state.detector),
            .colormap = static_cast<uint8_t>(state.colormap),
            .brightness = state.brightness,
            .gain = state.gain,
            .view = static_cast<uint8_t>(state.view),
        };
        nvs_handle_t handle;

        if (nvs_open(nvs_namespace, NVS_READWRITE, &handle) != ESP_OK)
            return;

        if (nvs_set_blob(handle, nvs_key, &s, sizeof(s)) == ESP_OK)
            nvs_commit(handle);

        nvs_close(handle);
    }
}
