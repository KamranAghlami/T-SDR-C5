// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/state.hpp"

namespace core
{
    enum class menu_item : uint8_t
    {
        view,
        span,
        step,
        detector,
        gain,
        colormap,
        brightness,
        version,
    };

    inline constexpr std::array<menu_item, 8> menu_items = {
        menu_item::view, menu_item::span,     menu_item::step,       menu_item::detector,
        menu_item::gain, menu_item::colormap, menu_item::brightness, menu_item::version,
    };

    inline constexpr std::array<uint8_t, 5> brightness_levels = {20, 40, 60, 80, 100};

    const char *menu_label(menu_item item);

    // Steps `item` of `state` to its next value; returns false if the item has
    // nothing to change. Manual gain steps by 8 up to `max_gain`.
    bool menu_change(state &state, menu_item item, unsigned max_gain);

    // Formats the current value of `item` into `out`.
    void menu_value(const state &state, menu_item item, char *out, size_t size, const char *version);
}
