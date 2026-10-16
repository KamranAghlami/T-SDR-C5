// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace core
{
    enum class gesture : uint8_t
    {
        none,
        short_a,
        short_b,
        long_a,
        long_b,
        both,
    };

    // Turns two sampled, active-high button levels into gestures. Call at a
    // steady rate (e.g. every 10 ms). Short presses fire on release, long
    // presses as soon as the threshold passes, and pressing both buttons fires
    // `both` once; nothing else fires until both are released again.
    class gesture_decoder
    {
    public:
        static constexpr uint32_t debounce_ms = 30;
        static constexpr uint32_t long_ms = 700;

        gesture update(uint32_t now_ms, bool a, bool b);

    private:
        struct debounced
        {
            bool stable = false;
            bool candidate = false;
            uint32_t since = 0;
            uint32_t pressed_at = 0;

            // True when the stable level changed in this update.
            bool sample(uint32_t now, bool raw);
        };

        debounced m_a, m_b;
        bool m_consumed = false;
    };
}
