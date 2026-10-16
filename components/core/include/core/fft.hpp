// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace core
{
    // In-place radix-2 decimation-in-time FFT on interleaved Q15 complex data.
    // Every stage halves the values (rounded), so the result is DFT / N and
    // stays in range for inputs within +-32767. Output is in natural order:
    // bin 0 is DC, bin N/2 is the Nyquist frequency.
    class fft_q15
    {
    public:
        explicit fft_q15(unsigned log2n);

        unsigned size() const { return m_n; }

        void forward(int16_t *data) const;

    private:
        unsigned m_n;
        std::vector<int16_t> m_twiddles; // cos, -sin pairs for k < N/2
        std::vector<uint16_t> m_swaps;   // bit-reversal swap pairs
    };
}
