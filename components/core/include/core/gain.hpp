// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace core
{
    // Gain setting values besides a manual PHY gain-table index.
    inline constexpr uint8_t gain_auto = 255; // auto_level below
    inline constexpr uint8_t gain_agc = 254;  // the PHY's own hardware AGC

    // Keeps a fixed manual gain just below clipping. Hardware AGC changes the
    // gain inside each Wi-Fi packet, which makes a waterfall pump; this only
    // moves between lines: fast down on clipping, slowly up when quiet.
    class auto_level
    {
    public:
        static constexpr unsigned clip_level = 480; // of 511, 10-bit samples
        static constexpr unsigned quiet_level = 128;
        static constexpr unsigned attack_step = 4;
        static constexpr unsigned release_step = 2;
        static constexpr unsigned release_lines = 50; // 2 s at 25 lines/s

        // New gain index after a line whose largest |sample| was `peak`.
        unsigned update(unsigned gain, unsigned max_gain, unsigned peak);

        void reset() { m_quiet = 0; }

    private:
        unsigned m_quiet = 0;
    };
}
