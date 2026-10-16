// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace core
{
    enum class band : uint8_t
    {
        ghz24,
        ghz5,
    };

    struct channel
    {
        uint16_t number;
        uint16_t mhz;
    };

    // 20 MHz Wi-Fi channel centres the ESP32-C5 radio can tune to.
    inline constexpr std::array<channel, 14> channels_24 = {{
        {1, 2412}, {2, 2417}, {3, 2422}, {4, 2427}, {5, 2432}, {6, 2437}, {7, 2442},
        {8, 2447}, {9, 2452}, {10, 2457}, {11, 2462}, {12, 2467}, {13, 2472}, {14, 2484},
    }};

    inline constexpr std::array<channel, 28> channels_5 = [] {
        constexpr std::array<uint16_t, 28> numbers = {36, 40, 44, 48, 52, 56, 60, 64, 100, 104,
                                                      108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
                                                      149, 153, 157, 161, 165, 169, 173, 177};
        std::array<channel, 28> table{};
        for (size_t i = 0; i < numbers.size(); i++)
            table[i] = {numbers[i], static_cast<uint16_t>(5000 + 5 * numbers[i])};
        return table;
    }();

    constexpr std::span<const channel> channels(band b)
    {
        return b == band::ghz24 ? std::span<const channel>(channels_24) : std::span<const channel>(channels_5);
    }

    constexpr const char *band_name(band b) { return b == band::ghz24 ? "2.4G" : "5G"; }

    constexpr band other(band b) { return b == band::ghz24 ? band::ghz5 : band::ghz24; }

    // Index of channel_index + step within the band, wrapping in both directions.
    constexpr unsigned step_channel(band b, unsigned index, int step)
    {
        const int count = static_cast<int>(channels(b).size());
        const int next = (static_cast<int>(index) + step) % count;
        return static_cast<unsigned>(next < 0 ? next + count : next);
    }
}
