// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/gestures.hpp"

namespace core
{
    bool gesture_decoder::debounced::sample(uint32_t now, bool raw)
    {
        if (raw != candidate)
        {
            candidate = raw;
            since = now;
            return false;
        }

        if (stable == candidate || now - since < debounce_ms)
            return false;

        stable = candidate;

        if (stable)
            pressed_at = now;

        return true;
    }

    gesture gesture_decoder::update(uint32_t now_ms, bool a, bool b)
    {
        const bool a_changed = m_a.sample(now_ms, a);
        const bool b_changed = m_b.sample(now_ms, b);

        if (!m_a.stable && !m_b.stable)
        {
            const bool was_consumed = m_consumed;

            m_consumed = false;

            if (was_consumed)
                return gesture::none;

            // Released without reaching the long threshold.
            if (a_changed)
                return gesture::short_a;

            if (b_changed)
                return gesture::short_b;

            return gesture::none;
        }

        if (m_consumed)
            return gesture::none;

        if (m_a.stable && m_b.stable)
        {
            m_consumed = true;

            return gesture::both;
        }

        if (m_a.stable && now_ms - m_a.pressed_at >= long_ms)
        {
            m_consumed = true;

            return gesture::long_a;
        }

        if (m_b.stable && now_ms - m_b.pressed_at >= long_ms)
        {
            m_consumed = true;

            return gesture::long_b;
        }

        return gesture::none;
    }
}
