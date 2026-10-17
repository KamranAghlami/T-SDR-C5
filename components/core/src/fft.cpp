// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fft.hpp"

#include <cmath>
#include <numbers>

namespace core
{
    namespace
    {
        inline int32_t half(int32_t v) { return (v + 1) >> 1; }

        // (x * w) in Q15, rounded: the real and imaginary parts.
        inline int32_t mul_re(int32_t xr, int32_t xi, int32_t wr, int32_t wi) { return (xr * wr - xi * wi + (1 << 14)) >> 15; }
        inline int32_t mul_im(int32_t xr, int32_t xi, int32_t wr, int32_t wi) { return (xr * wi + xi * wr + (1 << 14)) >> 15; }
    }

    fft_q15::fft_q15(unsigned log2n) : m_n(1u << log2n)
    {
        m_twiddles.resize(2 * m_n);
        m_order.resize(m_n);

        for (unsigned k = 0; k < m_n; k++)
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
        unsigned span = 4;

        // Two stages per pass. Stage spans h and 2h combine points a, b, c, d at
        // offsets 0, h, 2h, 3h of each group with W = e^(-2 pi i k / 4h):
        //   B = W^2k b, C = W^k c, D = W^3k d
        //   a' = (a + B) + (C + D)        c' = (a + B) - (C + D)
        //   b' = (a - B) - j (C - D)      d' = (a - B) + j (C - D)
        // all divided by 4 (two halving stages), rounded once.
        for (; span * 4 <= m_n; span *= 4)
        {
            const unsigned stride = m_n / (4 * span); // W^k = twiddles[k * stride]

            for (unsigned group = 0; group < m_n; group += 4 * span)
            {
                int16_t *a = x + 2 * group;
                int16_t *b = a + 2 * span;
                int16_t *c = b + 2 * span;
                int16_t *d = c + 2 * span;
                const int16_t *w1 = twiddles, *w2 = twiddles, *w3 = twiddles;

                for (unsigned k = 0; k < span; k++, a += 2, b += 2, c += 2, d += 2, w1 += 2 * stride, w2 += 4 * stride,
                              w3 += 6 * stride)
                {
                    const int32_t ar = a[0], ai = a[1];
                    const int32_t br = mul_re(b[0], b[1], w2[0], w2[1]), bi = mul_im(b[0], b[1], w2[0], w2[1]);
                    const int32_t cr = mul_re(c[0], c[1], w1[0], w1[1]), ci = mul_im(c[0], c[1], w1[0], w1[1]);
                    const int32_t dr = mul_re(d[0], d[1], w3[0], w3[1]), di = mul_im(d[0], d[1], w3[0], w3[1]);
                    const int32_t s0r = ar + br, s0i = ai + bi, s1r = ar - br, s1i = ai - bi;
                    const int32_t s2r = cr + dr, s2i = ci + di, s3r = cr - dr, s3i = ci - di;

                    a[0] = static_cast<int16_t>((s0r + s2r + 2) >> 2);
                    a[1] = static_cast<int16_t>((s0i + s2i + 2) >> 2);
                    c[0] = static_cast<int16_t>((s0r - s2r + 2) >> 2);
                    c[1] = static_cast<int16_t>((s0i - s2i + 2) >> 2);
                    // -j (s3r + j s3i) = s3i - j s3r
                    b[0] = static_cast<int16_t>((s1r + s3i + 2) >> 2);
                    b[1] = static_cast<int16_t>((s1i - s3r + 2) >> 2);
                    d[0] = static_cast<int16_t>((s1r - s3i + 2) >> 2);
                    d[1] = static_cast<int16_t>((s1i + s3r + 2) >> 2);
                }
            }
        }

        // An odd number of stages leaves one radix-2 stage.
        if (span < m_n)
        {
            for (unsigned k = 0; k < span; k++)
            {
                int16_t *a = x + 2 * k;
                int16_t *b = a + 2 * span;
                const int32_t wr = twiddles[2 * k], wi = twiddles[2 * k + 1];
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
