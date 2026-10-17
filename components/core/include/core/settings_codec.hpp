// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "core/state.hpp"

// The persistent settings blob (stored in NVS by main/settings.cpp). Fields are
// only ever appended, so every older layout stays readable:
//
//   byte  0  version         1..4
//         1  band            0 = 2.4G, 1 = 5G
//       2-3  channel index   per band (versions 1-3; zero since version 4)
//         4  span MHz
//         5  detector        0 = peak, 1 = mean
//         6  colour map      0 = turbo, 1 = grey
//         7  brightness %                          end of version 1 (8 bytes)
//         8  gain            255 auto, 254 HW AGC, else manual index   (v2: 9)
//         9  view            0 = detail, 1 = band                      (v3: 10)
//     10-13  frequency MHz   per band, little-endian
//        14  step MHz                                                  (v4: 15)
namespace core::settings_codec
{
    inline constexpr uint8_t version = 4;
    inline constexpr size_t size = 15;

    std::array<uint8_t, size> encode(const state &s);

    // The state a blob describes, starting from the defaults: out-of-range
    // values keep their default, and versions 1-3 convert their Wi-Fi channel
    // index to its frequency. Empty if the version or size is unknown.
    std::optional<state> decode(std::span<const uint8_t> blob);
}
