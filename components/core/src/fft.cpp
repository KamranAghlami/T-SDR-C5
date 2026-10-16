// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fft.hpp"

#include <cmath>
#include <numbers>

namespace core
{
    namespace
    {
        // Symmetric saturation keeps every product sum below INT32_MAX.
        inline int16_t sat(int32_t v) { return static_cast<int16_t>(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }
    }

    fft_q15::fft_q15(unsigned log2n) : m_n(1u << log2n)
    {
        m_twiddles.resize(m_n);

        for (unsigned k = 0; k < m_n / 2; k++)
        {
            const double phase = 2.0 * std::numbers::pi * k / m_n;

            m_twiddles[2 * k] = static_cast<int16_t>(std::lround(32767.0 * std::cos(phase)));
            m_twiddles[2 * k + 1] = static_cast<int16_t>(std::lround(-32767.0 * std::sin(phase)));
        }

        for (unsigned i = 0; i < m_n; i++)
        {
            unsigned r = 0;

            for (unsigned bit = 0; bit < log2n; bit++)
                r |= ((i >> bit) & 1u) << (log2n - 1 - bit);

            if (i < r)
            {
                m_swaps.push_back(static_cast<uint16_t>(i));
                m_swaps.push_back(static_cast<uint16_t>(r));
            }
        }
    }

    void fft_q15::forward(int16_t *x) const
    {
        auto *pairs = reinterpret_cast<uint32_t *>(x);

        for (size_t i = 0; i < m_swaps.size(); i += 2)
        {
            const uint32_t t = pairs[m_swaps[i]];

            pairs[m_swaps[i]] = pairs[m_swaps[i + 1]];
            pairs[m_swaps[i + 1]] = t;
        }

        for (unsigned half = 1, stride = m_n / 2; half < m_n; half *= 2, stride /= 2)
        {
            for (unsigned group = 0; group < m_n; group += 2 * half)
            {
                for (unsigned k = 0; k < half; k++)
                {
                    const int32_t wr = m_twiddles[2 * k * stride];
                    const int32_t wi = m_twiddles[2 * k * stride + 1];
                    int16_t *a = x + 2 * (group + k);
                    int16_t *b = a + 2 * half;
                    const int32_t br = b[0], bi = b[1];
                    // Product scaled by 2^15, then halved: one shift by 16.
                    const int32_t tr = (br * wr - bi * wi + (1 << 15)) >> 16;
                    const int32_t ti = (br * wi + bi * wr + (1 << 15)) >> 16;
                    const int32_t ar = (a[0] + 1) >> 1, ai = (a[1] + 1) >> 1;

                    a[0] = sat(ar + tr);
                    a[1] = sat(ai + ti);
                    b[0] = sat(ar - tr);
                    b[1] = sat(ai - ti);
                }
            }
        }
    }
}
