// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace core
{
    // In-place radix-2 decimation-in-time FFT on interleaved Q15 complex data
    // (N >= 8). Every stage halves the values (rounded), so the result is
    // DFT / N.
    //
    // Inputs must stay within +-16383 per component (a complex magnitude of at
    // most 23170): halving stages then can never overflow, which lets the
    // butterflies run without saturation. Output is in natural order: bin 0 is
    // DC, bin N/2 the Nyquist frequency.
    class fft_q15
    {
    public:
        static constexpr int16_t max_input = 16383;

        explicit fft_q15(unsigned log2n);

        unsigned size() const { return m_n; }

        // Input in natural order.
        void forward(int16_t *data) const;

        // Input already in bit-reversed order: sample j at index order()[j].
        void forward_permuted(int16_t *data) const;

        const uint16_t *order() const { return m_order.data(); }

    private:
        unsigned m_n;
        std::vector<int16_t> m_twiddles; // cos, -sin pairs for k < N/2
        std::vector<uint16_t> m_order;   // bit-reversed index of each sample
    };
}
