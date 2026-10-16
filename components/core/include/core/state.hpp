// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "core/channels.hpp"
#include "core/gain.hpp"
#include "core/spectrum.hpp"

namespace core
{
    // Receiver capture: 80 MS/s, 1024-point FFT, 78.125 kHz per bin.
    inline constexpr unsigned sample_rate_hz = 80'000'000;
    inline constexpr unsigned fft_log2 = 10;
    inline constexpr unsigned fft_size = 1u << fft_log2;

    inline constexpr std::array<uint8_t, 3> spans_mhz = {20, 40, 80};
    inline constexpr std::array<uint8_t, 4> steps_mhz = {1, 5, 10, 20};

    // Exact-tuning limits of the receive LO, as measured by ESPsoup (CW
    // generator, several boards): Wi-Fi, 13 cm amateur and 5.8 GHz FPV fit.
    struct tuning_range
    {
        uint16_t low_mhz;
        uint16_t high_mhz;
    };

    constexpr tuning_range range_of(band b) { return b == band::ghz24 ? tuning_range{2130, 2730} : tuning_range{4790, 5990}; }

    // mhz + delta within the band's range, wrapping at either end.
    constexpr unsigned step_frequency(band b, unsigned mhz, int delta)
    {
        const auto [low, high] = range_of(b);
        const int next = static_cast<int>(mhz) + delta;

        if (next > high)
            return low;

        if (next < low)
            return high;

        return static_cast<unsigned>(next);
    }

    constexpr unsigned span_bins(unsigned span_mhz)
    {
        return static_cast<unsigned>(static_cast<uint64_t>(span_mhz) * 1'000'000 * fft_size / sample_rate_hz);
    }

    enum class view_id : uint8_t
    {
        channel, // one channel, 20/40/80 MHz span
        band,    // the whole band, stitched from several captures
    };

    enum class colormap_id : uint8_t
    {
        turbo,
        grayscale,
    };

    // Everything the user can change; owned by the control loop.
    struct state
    {
        core::view_id view = core::view_id::channel;
        core::band band = core::band::ghz24;
        // Tuned frequency per band; in the band view it is the cursor.
        std::array<uint16_t, 2> frequency_mhz = {2437, 5180};
        uint8_t span_mhz = 40;
        uint8_t step_mhz = 5; // A/B tuning step
        core::detector detector = core::detector::peak;
        core::colormap_id colormap = core::colormap_id::turbo;
        uint8_t brightness = 80;
        uint8_t gain = gain_auto; // gain_auto, gain_agc or a manual index

        // Not persisted.
        bool frozen = false;
        bool menu_open = false;
        uint8_t menu_index = 0;

        unsigned current_mhz() const { return frequency_mhz[static_cast<unsigned>(band)]; }
    };
}
