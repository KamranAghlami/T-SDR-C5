// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/spectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace core
{
    namespace
    {
        // round(256 * log2(1 + i / 256))
        const std::array<uint16_t, 257> &log2_fraction_q8()
        {
            static const auto table = [] {
                std::array<uint16_t, 257> t{};

                for (unsigned i = 0; i <= 256; i++)
                    t[i] = static_cast<uint16_t>(std::lround(256.0 * std::log2(1.0 + i / 256.0)));

                return t;
            }();

            return table;
        }

        inline int16_t sat(int32_t v) { return static_cast<int16_t>(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }

        inline int32_t field(uint32_t word, unsigned shift)
        {
            return static_cast<int32_t>(word << (22 - shift)) >> 22;
        }
    }

    uint8_t power_code(uint64_t power)
    {
        if (!power)
            return 0;

        const unsigned exponent = 63u - static_cast<unsigned>(__builtin_clzll(power));
        const unsigned fraction = exponent >= 8 ? static_cast<unsigned>(power >> (exponent - 8)) & 255u
                                                : static_cast<unsigned>(power << (8 - exponent)) & 255u;
        const uint32_t log2_q8 = exponent * 256u + log2_fraction_q8()[fraction];
        // 2 * 10*log10(2) * 2^16 / 256 = 1541.1
        const uint32_t code = (log2_q8 * 1541u + 32768u) >> 16;

        return static_cast<uint8_t>(std::min<uint32_t>(code, 255));
    }

    std::vector<int16_t> hann_q15(unsigned n)
    {
        std::vector<int16_t> window(n);

        for (unsigned j = 0; j < n; j++)
            window[j] = static_cast<int16_t>(std::lround(16383.5 * (1.0 - std::cos(2.0 * std::numbers::pi * j / n))));

        return window;
    }

    unsigned prepare_iq(const uint32_t *words, unsigned n, const int16_t *window, int16_t *out)
    {
        int32_t sum_low = 0, sum_high = 0, peak = 0;

        for (unsigned j = 0; j < n; j++)
        {
            const int32_t low = field(words[j], 0), high = field(words[j], 10);

            sum_low += low;
            sum_high += high;
            peak = std::max({peak, low < 0 ? -low : low, high < 0 ? -high : high});
        }

        const int32_t half = static_cast<int32_t>(n / 2);
        const int32_t mean_low = (sum_low + (sum_low >= 0 ? half : -half)) / static_cast<int32_t>(n);
        const int32_t mean_high = (sum_high + (sum_high >= 0 ? half : -half)) / static_cast<int32_t>(n);

        for (unsigned j = 0; j < n; j++)
        {
            const int32_t w = window[j];

            // 10-bit samples times a Q15 window: >> 9 maps full scale to Q15.
            out[2 * j] = sat(((field(words[j], 0) - mean_low) * w) >> 9);
            out[2 * j + 1] = sat(((mean_high - field(words[j], 10)) * w) >> 9);
        }

        return static_cast<unsigned>(peak);
    }

    void line_accumulator::configure(unsigned n_fft, unsigned bins)
    {
        m_n = n_fft;
        m_bins = std::min(bins, n_fft);
        m_peak.assign(m_bins, 0);
        m_sum.assign(m_bins, 0);
        m_count = 0;
    }

    void line_accumulator::reset()
    {
        std::fill(m_peak.begin(), m_peak.end(), 0);
        std::fill(m_sum.begin(), m_sum.end(), 0);
        m_count = 0;
    }

    void line_accumulator::add(const int16_t *spectrum)
    {
        // Kept bin s covers frequency (s - bins/2) * fs / n.
        const unsigned first = m_n - m_bins / 2;

        for (unsigned s = 0; s < m_bins; s++)
        {
            const unsigned k = (first + s) & (m_n - 1);
            const int32_t re = spectrum[2 * k], im = spectrum[2 * k + 1];
            const uint32_t power = static_cast<uint32_t>(re * re) + static_cast<uint32_t>(im * im);

            m_peak[s] = std::max(m_peak[s], power);
            m_sum[s] += power;
        }

        m_count++;
    }

    void line_accumulator::finish(uint8_t *peak, uint8_t *mean, unsigned width)
    {
        const unsigned count = std::max(m_count, 1u);

        for (unsigned x = 0; x < width; x++)
        {
            const unsigned lo = x * m_bins / width;
            const unsigned hi = std::max(lo + 1, (x + 1) * m_bins / width);
            uint32_t p = 0;
            uint64_t s = 0;

            for (unsigned b = lo; b < hi; b++)
            {
                p = std::max(p, m_peak[b]);
                s = std::max(s, m_sum[b]);
            }

            peak[x] = power_code(p);
            mean[x] = power_code(s / count);
        }

        reset();
    }

    void floor_tracker::update(const uint8_t *codes, unsigned n)
    {
        if (!n)
            return;

        std::array<uint16_t, 256> histogram{};

        for (unsigned i = 0; i < n; i++)
            histogram[codes[i]]++;

        unsigned below = 0, percentile = 0;

        while (percentile < 255 && below + histogram[percentile] < n / 5)
            below += histogram[percentile++];

        if (!m_valid)
        {
            m_floor_q8 = percentile << 8;
            m_valid = true;
            return;
        }

        // Exponential smoothing over about 16 lines.
        m_floor_q8 = static_cast<uint32_t>(static_cast<int32_t>(m_floor_q8) +
                                           ((static_cast<int32_t>(percentile << 8) - static_cast<int32_t>(m_floor_q8)) >> 4));
    }
}
