// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/survey.hpp"

#include <algorithm>
#include <limits>

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
        m_first_px_q16.clear();

        for (const unsigned centre : plan.centres_mhz)
        {
            // Frequency of the first kept bin, relative to the line's left edge.
            const int64_t first_hz = (static_cast<int64_t>(centre) - plan.low_mhz) * 1'000'000 -
                                     static_cast<int64_t>(m_keep / 2 * bin_hz);

            m_first_px_q16.push_back(static_cast<int32_t>((first_hz * width << 16) / static_cast<int64_t>(span_hz)));
        }

        m_peak.assign(width, 0);
        m_sum.assign(width, 0);
        m_count.assign(width, 0);
        m_step_sum.assign(m_keep, 0);
        m_line_min.assign(m_keep, std::numeric_limits<uint64_t>::max());
        m_shape_q8.assign(m_keep, 0);
        m_correction_q8.assign(m_keep, 0);
        m_gain_q8.assign(m_keep, 256);
        m_shape_valid = false;
        m_step = -1;
        m_step_captures = 0;
        m_captures = 0;
    }

    void survey_accumulator::reset_passband()
    {
        std::fill(m_correction_q8.begin(), m_correction_q8.end(), 0);
        std::fill(m_gain_q8.begin(), m_gain_q8.end(), 256);
        m_shape_valid = false;
    }

    void survey_accumulator::reset()
    {
        std::fill(m_peak.begin(), m_peak.end(), 0);
        std::fill(m_sum.begin(), m_sum.end(), 0);
        std::fill(m_count.begin(), m_count.end(), 0);
        std::fill(m_step_sum.begin(), m_step_sum.end(), 0);
        std::fill(m_line_min.begin(), m_line_min.end(), std::numeric_limits<uint64_t>::max());
        m_step = -1;
        m_step_captures = 0;
        m_captures = 0;
    }

    void survey_accumulator::close_step()
    {
        if (m_step < 0 || !m_step_captures)
            return;

        for (unsigned s = 0; s < m_keep; s++)
        {
            m_line_min[s] = std::min(m_line_min[s], m_step_sum[s] / m_step_captures);
            m_step_sum[s] = 0;
        }

        m_step_captures = 0;
    }

    void survey_accumulator::add(const int16_t *spectrum, unsigned scale, unsigned step)
    {
        if (step >= m_first_px_q16.size())
            return;

        if (static_cast<int>(step) != m_step)
        {
            close_step();
            m_step = static_cast<int>(step);
        }

        const unsigned shift = 2 * (max_scale - std::min(scale, max_scale));
        const unsigned first_bin = m_n - m_keep / 2;
        const unsigned centre = m_keep / 2;
        int32_t px_q16 = m_first_px_q16[step];

        for (unsigned s = 0; s < m_keep; s++, px_q16 += m_inc_q16)
        {
            const unsigned k = (first_bin + s) & (m_n - 1);
            const int32_t re = spectrum[2 * k], im = spectrum[2 * k + 1];
            const uint64_t power = static_cast<uint64_t>(static_cast<uint32_t>(re * re) + static_cast<uint32_t>(im * im))
                                   << shift;
            const int32_t px = px_q16 >> 16;

            m_step_sum[s] += power;

            if (px < 0 || px >= static_cast<int32_t>(m_width) || (s + dc_guard_bins >= centre && s <= centre + dc_guard_bins))
                continue;

            const uint64_t flat = power * m_gain_q8[s] >> 8;

            m_peak[px] = std::max(m_peak[px], flat);
            m_sum[px] += flat;
            m_count[px]++;
        }

        m_step_captures++;
        m_captures++;
    }

    void survey_accumulator::finish(uint8_t *peak, uint8_t *mean)
    {
        close_step();

        // Update the passband estimate from this line's quietest steps.
        if (m_line_min[0] != std::numeric_limits<uint64_t>::max())
        {
            int64_t total = 0;

            for (unsigned s = 0; s < m_keep; s++)
            {
                const int32_t level = static_cast<int32_t>(log2_q8(std::max<uint64_t>(m_line_min[s], 1)));

                // About 16 lines of smoothing; the first line sets the estimate.
                m_shape_q8[s] = m_shape_valid ? m_shape_q8[s] + ((level - m_shape_q8[s]) >> 4) : level;
                total += m_shape_q8[s];
            }

            m_shape_valid = true;

            // Normalize to the average level so overall brightness is unchanged.
            const int32_t reference = static_cast<int32_t>(total / static_cast<int64_t>(m_keep));

            for (unsigned s = 0; s < m_keep; s++)
            {
                m_correction_q8[s] = std::clamp(reference - m_shape_q8[s], -max_correction_q8, max_correction_q8);
                m_gain_q8[s] = exp2_q8(m_correction_q8[s]);
            }
        }

        // Pixels covered only by guard bins take the mean of their neighbours.
        for (unsigned x = 0; x < m_width; x++)
        {
            if (m_count[x])
                continue;

            const unsigned left = x > 0 ? x - 1 : x + 1, right = x + 1 < m_width ? x + 1 : x - 1;

            if (left < m_width && right < m_width && m_count[left] && m_count[right])
            {
                m_peak[x] = (m_peak[left] + m_peak[right]) / 2;
                m_sum[x] = m_sum[left] / m_count[left] / 2 + m_sum[right] / m_count[right] / 2;
                m_count[x] = 1;
            }
        }

        for (unsigned x = 0; x < m_width; x++)
        {
            peak[x] = spectrum_code(m_peak[x]);
            mean[x] = spectrum_code(m_count[x] ? m_sum[x] / m_count[x] : 0);
        }

        reset();
    }
}
