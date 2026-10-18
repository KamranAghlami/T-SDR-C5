// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"

#include <nvs.h>

#include "board/pins.hpp"
#include "core/settings_codec.hpp"

namespace settings
{
    namespace
    {
        // Lowercase from before the project was renamed T-SDR-C5; changing it
        // would orphan every board's saved settings.
        constexpr const char *nvs_namespace = "t-sdr-c5";
        constexpr const char *nvs_key = "state";
    }

    core::state load()
    {
        nvs_handle_t handle;

        if (nvs_open(nvs_namespace, NVS_READONLY, &handle) != ESP_OK)
            return {};

        // Room for larger future layouts too; decode() rejects what it doesn't know.
        uint8_t blob[64];
        size_t size = sizeof(blob);
        const bool read = nvs_get_blob(handle, nvs_key, blob, &size) == ESP_OK;

        nvs_close(handle);

        core::state state = read ? core::settings_codec::decode({blob, size}).value_or(core::state{}) : core::state{};

        if (!board::dual_band)
            state.band = core::band::ghz24;

        return state;
    }

    void save(const core::state &state)
    {
        const auto blob = core::settings_codec::encode(state);
        nvs_handle_t handle;

        if (nvs_open(nvs_namespace, NVS_READWRITE, &handle) != ESP_OK)
            return;

        if (nvs_set_blob(handle, nvs_key, blob.data(), blob.size()) == ESP_OK)
            nvs_commit(handle);

        nvs_close(handle);
    }
}
