// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/gain.hpp"

#include <algorithm>

namespace core
{
    unsigned auto_level::update(unsigned gain, unsigned max_gain, unsigned peak)
    {
        gain = std::min(gain, max_gain);

        if (peak >= clip_level)
        {
            m_quiet = 0;

            return gain > attack_step ? gain - attack_step : 0;
        }

        if (peak >= quiet_level)
        {
            m_quiet = 0;

            return gain;
        }

        if (++m_quiet < release_lines)
            return gain;

        m_quiet = 0;

        return std::min(gain + release_step, max_gain);
    }
}
