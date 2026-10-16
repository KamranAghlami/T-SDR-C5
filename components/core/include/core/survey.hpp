// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/channels.hpp"

namespace core
{
    // Band survey: the receiver steps across a band in 40 MHz chunks and keeps
    // the flat +-20 MHz middle of each capture; the chunks together form one
    // waterfall line.
    inline constexpr unsigned survey_step_mhz = 40;

    struct survey_plan
    {
        uint16_t low_mhz;  // left edge of the line
        uint16_t high_mhz; // right edge of the line
        std::vector<uint16_t> centres_mhz;
    };

    // The band's whole tuning range: 2130-2730 MHz in 15 steps, 4790-5990 MHz in 30.
    survey_plan make_survey_plan(band b);

    // Pixel (0..width-1) of a frequency on the survey line, or -1 outside it.
    int survey_pixel(const survey_plan &plan, unsigned width, unsigned mhz);

    // Accumulates the kept bins of every step of a survey line into pixels.
    //
    // Every step sees the same receiver passband: noise is stronger at the
    // centre of a capture than at its edges, which would draw one hump per
    // step. The accumulator learns that shape from the quietest step at each
    // bin offset (a signal rarely sits at the same offset in every step),
    // smooths it over lines and divides it out. The few bins around each
    // step's centre carry residual DC/LO leakage; they are skipped and the
    // pixels filled from their neighbours.
    class survey_accumulator
    {
    public:
        // Bins either side of DC left out of the line.
        static constexpr unsigned dc_guard_bins = 2;
        // Passband correction is limited to +-12 dB.
        static constexpr int32_t max_correction_q8 = 4 * 256;

        void configure(const survey_plan &plan, unsigned n_fft, unsigned sample_rate_hz, unsigned width);

        // FFT of a capture taken at plan.centres_mhz[step]; `scale` from prepare_iq.
        // Captures arrive step by step (all of step 0, then step 1, ...).
        void add(const int16_t *spectrum, unsigned scale, unsigned step);

        // Peak and mean codes per pixel (as line_accumulator), then starts a new line.
        void finish(uint8_t *peak, uint8_t *mean);

        unsigned captures() const { return m_captures; }

        // Forget the learned passband (after a gain or band change).
        void reset_passband();

        void reset();

        // Passband correction per kept bin, log2 Q8 (for tests and diagnostics).
        const std::vector<int32_t> &correction() const { return m_correction_q8; }

    private:
        void close_step();

        unsigned m_n = 0;
        unsigned m_width = 0;
        unsigned m_keep = 0;   // bins kept per capture, centred on DC
        int32_t m_inc_q16 = 0; // pixels per bin, Q16
        unsigned m_captures = 0;
        std::vector<int32_t> m_first_px_q16; // pixel of each step's first kept bin, Q16

        // Pixels of the line being built.
        std::vector<uint64_t> m_peak;
        std::vector<uint64_t> m_sum;
        std::vector<uint32_t> m_count;

        // Passband estimate, per kept bin.
        int m_step = -1;
        unsigned m_step_captures = 0;
        std::vector<uint64_t> m_step_sum;   // raw power of the current step
        std::vector<uint64_t> m_line_min;   // quietest step mean of this line
        std::vector<int32_t> m_shape_q8;    // smoothed log2 of the passband
        std::vector<int32_t> m_correction_q8;
        std::vector<uint32_t> m_gain_q8;    // linear correction factor
        bool m_shape_valid = false;
    };
}
