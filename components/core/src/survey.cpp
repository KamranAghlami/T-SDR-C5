// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/survey.hpp"

#include <algorithm>

#include "core/spectrum.hpp"

namespace core
{
    survey_plan make_survey_plan(band b)
    {
        survey_plan plan = b == band::ghz24 ? survey_plan{2400, 2500, {}} : survey_plan{5150, 5895, {}};
        const unsigned half = survey_step_mhz / 2;

        for (unsigned centre = plan.low_mhz + half; centre - half < plan.high_mhz; centre += survey_step_mhz)
            plan.centres_mhz.push_back(static_cast<uint16_t>(centre));

        return plan;
    }

    int survey_pixel(const survey_plan &plan, unsigned width, unsigned mhz)
    {
        if (mhz < plan.low_mhz || mhz >= plan.high_mhz)
            return -1;

        return static_cast<int>((mhz - plan.low_mhz) * width / (plan.high_mhz - plan.low_mhz));
    }

    void survey_accumulator::configure(const survey_plan &plan, unsigned n_fft, unsigned sample_rate_hz, unsigned width)
    {
        const uint64_t span_hz = (plan.high_mhz - plan.low_mhz) * 1'000'000ull;
        const uint64_t bin_hz = sample_rate_hz / n_fft;

        m_n = n_fft;
        m_width = width;
        m_keep = static_cast<unsigned>(survey_step_mhz * 1'000'000ull / bin_hz);
        m_inc_q16 = static_cast<int32_t>((bin_hz * width << 16) / span_hz);
        m_steps.clear();

        for (const unsigned centre : plan.centres_mhz)
        {
            // Frequency of the first kept bin, relative to the line's left edge.
            const int64_t first_hz = (static_cast<int64_t>(centre) - plan.low_mhz) * 1'000'000 -
                                     static_cast<int64_t>(m_keep / 2 * bin_hz);

            m_steps.push_back({static_cast<int32_t>((first_hz * width << 16) / static_cast<int64_t>(span_hz))});
        }

        m_peak.assign(width, 0);
        m_sum.assign(width, 0);
        m_count.assign(width, 0);
        m_captures = 0;
    }

    void survey_accumulator::reset()
    {
        std::fill(m_peak.begin(), m_peak.end(), 0);
        std::fill(m_sum.begin(), m_sum.end(), 0);
        std::fill(m_count.begin(), m_count.end(), 0);
        m_captures = 0;
    }

    void survey_accumulator::add(const int16_t *spectrum, unsigned scale, unsigned step)
    {
        if (step >= m_steps.size())
            return;

        const unsigned shift = 2 * (max_scale - std::min(scale, max_scale));
        const unsigned first_bin = m_n - m_keep / 2;
        int32_t px_q16 = m_steps[step].first_px_q16;

        for (unsigned s = 0; s < m_keep; s++, px_q16 += m_inc_q16)
        {
            const int32_t px = px_q16 >> 16;

            if (px < 0 || px >= static_cast<int32_t>(m_width))
                continue;

            const unsigned k = (first_bin + s) & (m_n - 1);
            const int32_t re = spectrum[2 * k], im = spectrum[2 * k + 1];
            const uint64_t power = static_cast<uint64_t>(static_cast<uint32_t>(re * re) + static_cast<uint32_t>(im * im))
                                   << shift;

            m_peak[px] = std::max(m_peak[px], power);
            m_sum[px] += power;
            m_count[px]++;
        }

        m_captures++;
    }

    void survey_accumulator::finish(uint8_t *peak, uint8_t *mean)
    {
        for (unsigned x = 0; x < m_width; x++)
        {
            peak[x] = spectrum_code(m_peak[x]);
            mean[x] = spectrum_code(m_count[x] ? m_sum[x] / m_count[x] : 0);
        }

        reset();
    }
}
