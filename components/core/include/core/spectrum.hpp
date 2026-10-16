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

    // Power in 0.5 dB steps: 2 * 10*log10(power / 2^bias_log2), clamped to
    // 0..255. Within 0.3 dB including the 0.5 dB quantization.
    uint8_t power_code(uint64_t power, unsigned bias_log2 = 0);

    // Hann window in Q15, peak 32767.
    std::vector<int16_t> hann_q15(unsigned n);

    // Block floating point: each capture is scaled by 2^scale (largest that
    // keeps it within the fft_q15 input range), so weak signals use all 16
    // bits instead of drowning in the FFT's rounding noise.
    inline constexpr unsigned max_scale = 13;

    struct iq_block
    {
        unsigned peak;  // largest |sample| before DC removal (0..512), for gain control
        unsigned scale; // samples were multiplied by 2^scale, 0..max_scale
    };

    // Converts n IQ10 dump words (bits 0..9 and 10..19, signed) into windowed
    // interleaved complex samples with the mean (DC/LO leakage) removed,
    // scaled into +-fft_q15::max_input. Sample j is written to out[order[j]]
    // (pass fft_q15::order() and use forward_permuted()). The dump delivers
    // LO - RF, so the second field is negated: positive frequency offsets then
    // land above the centre, as on the air.
    iq_block prepare_iq(const uint32_t *words, unsigned n, const int16_t *window, const uint16_t *order, int16_t *out);

    // Collects FFT power spectra for one waterfall line and reduces them to
    // display pixels.
    class line_accumulator
    {
    public:
        // Keep the `bins` bins around DC of an n_fft-point spectrum.
        void configure(unsigned n_fft, unsigned bins);

        // `scale` from prepare_iq: powers are normalized to a common scale.
        void add(const int16_t *spectrum, unsigned scale);

        unsigned count() const { return m_count; }

        // Peak-hold and mean codes per pixel (each pixel is the maximum of
        // the bins it covers), then starts a new line.
        void finish(uint8_t *peak, uint8_t *mean, unsigned width);

        void reset();

    private:
        unsigned m_n = 0;
        unsigned m_bins = 0;
        unsigned m_count = 0;
        std::vector<uint64_t> m_peak;
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
