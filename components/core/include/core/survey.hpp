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

    // 2.4 GHz: 2400-2500 MHz in 3 steps. 5 GHz: 5150-5895 MHz (UNII-1 to 4) in 19.
    survey_plan make_survey_plan(band b);

    // Pixel (0..width-1) of a frequency on the survey line, or -1 outside it.
    int survey_pixel(const survey_plan &plan, unsigned width, unsigned mhz);

    // Accumulates the kept bins of every step of a survey line into pixels.
    class survey_accumulator
    {
    public:
        void configure(const survey_plan &plan, unsigned n_fft, unsigned sample_rate_hz, unsigned width);

        // FFT of a capture taken at plan.centres_mhz[step]; `scale` from prepare_iq.
        void add(const int16_t *spectrum, unsigned scale, unsigned step);

        // Peak and mean codes per pixel (as line_accumulator), then starts a new line.
        void finish(uint8_t *peak, uint8_t *mean);

        unsigned captures() const { return m_captures; }

        void reset();

    private:
        struct step_map
        {
            int32_t first_px_q16; // pixel of the first kept bin, Q16
        };

        unsigned m_n = 0;
        unsigned m_width = 0;
        unsigned m_keep = 0;    // bins kept per capture, centred on DC
        int32_t m_inc_q16 = 0; // pixels per bin, Q16
        unsigned m_captures = 0;
        std::vector<step_map> m_steps;
        std::vector<uint64_t> m_peak;
        std::vector<uint64_t> m_sum;
        std::vector<uint32_t> m_count;
    };
}
