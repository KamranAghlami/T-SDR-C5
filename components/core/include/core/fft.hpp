// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace core
{
    // In-place decimation-in-time FFT on interleaved Q15 complex data (N >= 4,
    // a power of two). Input in bit-reversed order; stages are fused in pairs
    // into radix-4 passes (3 complex multiplies per 4 points instead of 4, one
    // pass over memory per two stages), with a final radix-2 stage when log2 N
    // is odd. Every stage halves the values (rounded once per pass), so the
    // result is DFT / N.
    //
    // Inputs must stay within +-16384 per component (a complex magnitude of at
    // most 23171): halving stages never raise the largest magnitude, and int16
    // only overflows above 32767, so the butterflies run without saturation.
    // Output is in natural order: bin 0 is DC, bin N/2 the Nyquist frequency.
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
        std::vector<int16_t> m_twiddles; // cos, -sin pairs of W^k for k < N (radix-4 uses up to 3k)
        std::vector<uint16_t> m_order;   // bit-reversed index of each sample
    };
}
