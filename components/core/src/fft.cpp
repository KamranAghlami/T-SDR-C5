// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fft.hpp"

#include <cmath>
#include <numbers>

namespace core
{
    namespace
    {
        inline int32_t half(int32_t v) { return (v + 1) >> 1; }
    }

    fft_q15::fft_q15(unsigned log2n) : m_n(1u << log2n)
    {
        m_twiddles.resize(m_n);
        m_order.resize(m_n);

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

            m_order[i] = static_cast<uint16_t>(r);
        }
    }

    void fft_q15::forward(int16_t *x) const
    {
        auto *pairs = reinterpret_cast<uint32_t *>(x);

        for (unsigned i = 0; i < m_n; i++)
        {
            const unsigned r = m_order[i];

            if (i < r)
            {
                const uint32_t t = pairs[i];

                pairs[i] = pairs[r];
                pairs[r] = t;
            }
        }

        forward_permuted(x);
    }

    void fft_q15::forward_permuted(int16_t *x) const
    {
        // Stages 1 and 2 as one radix-4 pass: their twiddles are 1 and -j.
        for (unsigned g = 0; g < m_n; g += 4)
        {
            int16_t *p = x + 2 * g;
            const int32_t a0r = half(p[0] + p[2]), a0i = half(p[1] + p[3]);
            const int32_t a1r = half(p[0] - p[2]), a1i = half(p[1] - p[3]);
            const int32_t a2r = half(p[4] + p[6]), a2i = half(p[5] + p[7]);
            const int32_t a3r = half(p[4] - p[6]), a3i = half(p[5] - p[7]);
            const int32_t b0r = half(a0r), b0i = half(a0i), b2r = half(a2r), b2i = half(a2i);
            const int32_t b1r = half(a1r), b1i = half(a1i);
            // a3 * -j = (a3i, -a3r)
            const int32_t b3r = half(a3i), b3i = half(-a3r);

            p[0] = static_cast<int16_t>(b0r + b2r);
            p[1] = static_cast<int16_t>(b0i + b2i);
            p[4] = static_cast<int16_t>(b0r - b2r);
            p[5] = static_cast<int16_t>(b0i - b2i);
            p[2] = static_cast<int16_t>(b1r + b3r);
            p[3] = static_cast<int16_t>(b1i + b3i);
            p[6] = static_cast<int16_t>(b1r - b3r);
            p[7] = static_cast<int16_t>(b1i - b3i);
        }

        const int16_t *const twiddles = m_twiddles.data();

        for (unsigned span = 4, stride = m_n / 8; span < m_n; span *= 2, stride /= 2)
        {
            for (unsigned group = 0; group < m_n; group += 2 * span)
            {
                int16_t *a = x + 2 * group;
                int16_t *b = a + 2 * span;
                const int16_t *w = twiddles;

                for (unsigned k = 0; k < span; k++, a += 2, b += 2, w += 2 * stride)
                {
                    const int32_t wr = w[0], wi = w[1];
                    const int32_t br = b[0], bi = b[1];
                    // Product scaled by 2^15, then halved: one shift by 16.
                    const int32_t tr = (br * wr - bi * wi + (1 << 15)) >> 16;
                    const int32_t ti = (br * wi + bi * wr + (1 << 15)) >> 16;
                    const int32_t ar = half(a[0]), ai = half(a[1]);

                    a[0] = static_cast<int16_t>(ar + tr);
                    a[1] = static_cast<int16_t>(ai + ti);
                    b[0] = static_cast<int16_t>(ar - tr);
                    b[1] = static_cast<int16_t>(ai - ti);
                }
            }
        }
    }
}
