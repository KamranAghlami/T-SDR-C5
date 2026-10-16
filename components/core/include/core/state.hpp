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
        // Selected channel per band; in the band view it is the cursor.
        std::array<uint8_t, 2> channel_index = {5, 0}; // ch 6, ch 36
        uint8_t span_mhz = 40;
        core::detector detector = core::detector::peak;
        core::colormap_id colormap = core::colormap_id::turbo;
        uint8_t brightness = 80;
        uint8_t gain = gain_auto; // gain_auto, gain_agc or a manual index

        // Not persisted.
        bool frozen = false;
        bool menu_open = false;
        uint8_t menu_index = 0;

        const core::channel &current_channel() const
        {
            return channels(band)[channel_index[static_cast<unsigned>(band)]];
        }
    };
}
