// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/spectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
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

        constexpr unsigned code_bias_log2 = 12;

        inline int32_t field(uint32_t word, unsigned shift)
        {
            return static_cast<int32_t>(word << (22 - shift)) >> 22;
        }
    }

    uint32_t log2_q8(uint64_t value)
    {
        if (!value)
            return 0;

        const unsigned exponent = 63u - static_cast<unsigned>(__builtin_clzll(value));
        const unsigned fraction = exponent >= 8 ? static_cast<unsigned>(value >> (exponent - 8)) & 255u
                                                : static_cast<unsigned>(value << (8 - exponent)) & 255u;

        return exponent * 256u + log2_fraction_q8()[fraction];
    }

    uint32_t exp2_q8(int32_t log2_q8_value)
    {
        // round(65536 * 2^(i / 256)) for the fractional part.
        static const auto table = [] {
            std::array<uint32_t, 256> t{};

            for (unsigned i = 0; i < 256; i++)
                t[i] = static_cast<uint32_t>(std::lround(65536.0 * std::exp2(i / 256.0)));

            return t;
        }();
        const int32_t whole = log2_q8_value >> 8; // floor
        const uint32_t mantissa = table[static_cast<unsigned>(log2_q8_value) & 255u]; // 2^frac in Q16
        const int shift = 8 + whole - 16; // result in Q8

        // The mantissa uses up to 17 bits, so shifts above 14 would overflow.
        if (shift >= 0)
            return shift > 14 ? UINT32_MAX : mantissa << shift;

        return shift < -31 ? 0 : (mantissa + (1u << (-shift - 1))) >> -shift;
    }

    uint8_t power_code(uint64_t power, unsigned bias_log2)
    {
        if (!power)
            return 0;

        const uint32_t level = log2_q8(power);

        if (level <= bias_log2 * 256u)
            return 0;

        // 2 * 10*log10(2) * 2^16 / 256 = 1541.1
        const uint32_t code = ((level - bias_log2 * 256u) * 1541u + 32768u) >> 16;

        return static_cast<uint8_t>(std::min<uint32_t>(code, 255));
    }

    uint8_t spectrum_code(uint64_t normalized_power) { return power_code(normalized_power, code_bias_log2); }

    std::vector<int16_t> hann_q15(unsigned n)
    {
        std::vector<int16_t> window(n);

        for (unsigned j = 0; j < n; j++)
            window[j] = static_cast<int16_t>(std::lround(16383.5 * (1.0 - std::cos(2.0 * std::numbers::pi * j / n))));

        return window;
    }

    iq_block prepare_iq(const uint32_t *words, unsigned n, const int16_t *window, const uint16_t *order, int16_t *out)
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
        // Bound on |sample - mean|; the window is at most 1.
        const int32_t bound = std::max<int32_t>(1, peak + std::max(std::abs(mean_low), std::abs(mean_high)));
        unsigned scale = 0;

        while (scale < max_scale && (bound << (scale + 1)) <= 16383)
            scale++;

        const unsigned shift = 15 - scale;

        for (unsigned j = 0; j < n; j++)
        {
            const int32_t w = window[j];
            int16_t *o = out + 2 * order[j];

            // |sample - mean| * 2^scale <= 16383 by the choice of scale; flooring a
            // negative product can reach -16384, which the FFT still handles
            // (its halving stages only need complex magnitudes below 32768).
            o[0] = static_cast<int16_t>(((field(words[j], 0) - mean_low) * w) >> shift);
            o[1] = static_cast<int16_t>(((mean_high - field(words[j], 10)) * w) >> shift);
        }

        return {static_cast<unsigned>(peak), scale};
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

    void line_accumulator::add(const int16_t *spectrum, unsigned scale)
    {
        // Kept bin s covers frequency (s - bins/2) * fs / n.
        const unsigned first = m_n - m_bins / 2;
        const unsigned shift = 2 * (max_scale - std::min(scale, max_scale));

        for (unsigned s = 0; s < m_bins; s++)
        {
            const unsigned k = (first + s) & (m_n - 1);
            const int32_t re = spectrum[2 * k], im = spectrum[2 * k + 1];
            const uint64_t power = static_cast<uint64_t>(static_cast<uint32_t>(re * re) + static_cast<uint32_t>(im * im))
                                   << shift;

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
            uint64_t p = 0, s = 0;

            for (unsigned b = lo; b < hi; b++)
            {
                p = std::max(p, m_peak[b]);
                s = std::max(s, m_sum[b]);
            }

            peak[x] = spectrum_code(p);
            mean[x] = spectrum_code(s / count);
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
