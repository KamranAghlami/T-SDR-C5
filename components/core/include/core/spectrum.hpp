// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace core
{
    enum class detector : uint8_t
    {
        peak,
        mean,
    };

    // Power in 0.5 dB steps (2 * 10*log10(power)), 0 for zero power, at most
    // 255. Within 0.3 dB including the 0.5 dB quantization.
    uint8_t power_code(uint64_t power);

    // Hann window in Q15, peak 32767.
    std::vector<int16_t> hann_q15(unsigned n);

    // Converts n IQ10 dump words (bits 0..9 and 10..19, signed) into windowed
    // interleaved Q15 complex samples with the mean (DC/LO leakage) removed.
    // The dump delivers LO - RF, so the second field is negated: positive
    // frequency offsets then land above the centre, as on the air. Returns
    // the largest |sample| before DC removal (0..512), for gain control.
    unsigned prepare_iq(const uint32_t *words, unsigned n, const int16_t *window, int16_t *out);

    // Collects FFT power spectra for one waterfall line and reduces them to
    // display pixels.
    class line_accumulator
    {
    public:
        // Keep the `bins` bins around DC of an n_fft-point spectrum.
        void configure(unsigned n_fft, unsigned bins);

        void add(const int16_t *spectrum);

        unsigned count() const { return m_count; }

        // Peak-hold and mean codes per pixel (each pixel is the maximum of
        // the bins it covers), then starts a new line.
        void finish(uint8_t *peak, uint8_t *mean, unsigned width);

        void reset();

    private:
        unsigned m_n = 0;
        unsigned m_bins = 0;
        unsigned m_count = 0;
        std::vector<uint32_t> m_peak;
        std::vector<uint64_t> m_sum;
    };

    // Tracks the noise floor as a smoothed 20th percentile of a line's codes.
    class floor_tracker
    {
    public:
        void update(const uint8_t *codes, unsigned n);

        void reset() { m_valid = false; }

        unsigned floor() const { return (m_floor_q8 + 128) >> 8; }

    private:
        uint32_t m_floor_q8 = 0;
        bool m_valid = false;
    };
}
