// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/state.hpp"

// Persists the user-facing state in NVS. Requires nvs_flash_init().
namespace settings
{
    core::state load();

    void save(const core::state &state);
}
