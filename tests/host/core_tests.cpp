// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for components/core (no ESP-IDF).
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "core/channels.hpp"
#include "core/colormap.hpp"
#include "core/fft.hpp"
#include "core/gain.hpp"
#include "core/gestures.hpp"
#include "core/menu.hpp"
#include "core/settings_codec.hpp"
#include "core/spectrum.hpp"
#include "core/state.hpp"
#include "core/survey.hpp"

namespace
{
    int failures = 0;

#define CHECK(...)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(__VA_ARGS__))                                                                                                \
        {                                                                                                              \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #__VA_ARGS__);                                   \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

    constexpr double pi = std::numbers::pi;

    // Packs a complex sample as the dump engine delivers it (LO - RF): the
    // low field carries sin, the high field cos of the on-air phase.
    uint32_t dump_word(double phase, double amplitude)
    {
        const auto low = static_cast<int32_t>(std::lround(amplitude * std::sin(phase)));
        const auto high = static_cast<int32_t>(std::lround(amplitude * std::cos(phase)));

        return (static_cast<uint32_t>(low) & 0x3ffu) | ((static_cast<uint32_t>(high) & 0x3ffu) << 10);
    }

    unsigned argmax_power(const int16_t *x, unsigned n)
    {
        unsigned best = 0;
        double best_power = -1;

        for (unsigned k = 0; k < n; k++)
        {
            const double p = double(x[2 * k]) * x[2 * k] + double(x[2 * k + 1]) * x[2 * k + 1];

            if (p > best_power)
            {
                best_power = p;
                best = k;
            }
        }

        return best;
    }

    // Even and odd stage counts (radix-4 passes, plus a radix-2 stage when odd).
    void test_fft_matches_dft()
    {
        for (const unsigned log2n : {2u, 3u, 8u, 9u, 10u})
        {
            const unsigned n = 1u << log2n;
            const core::fft_q15 fft(log2n);
            std::vector<int16_t> x(2 * n);
            std::vector<std::complex<double>> input(n);

            // Two tones plus an offset, within the FFT's input range.
            for (unsigned j = 0; j < n; j++)
            {
                input[j] = 9000.0 * std::polar(1.0, 2 * pi * (n / 20 + 1) * j / n) +
                           4000.0 * std::polar(1.0, -2 * pi * (n * 0.27 + 0.5) * j / n) + std::complex<double>(1200, -800);
                x[2 * j] = static_cast<int16_t>(std::lround(input[j].real()));
                x[2 * j + 1] = static_cast<int16_t>(std::lround(input[j].imag()));
            }

            fft.forward(x.data());

            double worst = 0;

            for (unsigned k = 0; k < n; k++)
            {
                std::complex<double> sum;

                for (unsigned j = 0; j < n; j++)
                    sum += input[j] * std::polar(1.0, -2 * pi * k * j / n);

                sum /= n;
                worst = std::max(worst, std::abs(sum - std::complex<double>(x[2 * k], x[2 * k + 1])));
            }

            std::printf("fft %4u: worst bin error %.2f LSB\n", n, worst);
            CHECK(worst < 4.0);
        }
    }

    void test_fft_full_scale_does_not_wrap()
    {
        constexpr unsigned log2n = 10, n = 1u << log2n;
        const core::fft_q15 fft(log2n);
        std::vector<int16_t> x(2 * n);

        for (unsigned j = 0; j < n; j++)
        {
            x[2 * j] = core::fft_q15::max_input;
            x[2 * j + 1] = -core::fft_q15::max_input;
        }

        fft.forward(x.data());
        CHECK(x[0] > 16000 && x[1] < -16000);
        CHECK(argmax_power(x.data(), n) == 0);
    }

    // Thermal noise of a few LSB must not drown in fixed-point rounding: the
    // mean power over all bins has to match a double-precision reference.
    void test_noise_floor_precision()
    {
        const unsigned n = core::fft_size;
        const core::fft_q15 fft(core::fft_log2);
        const auto window = core::hann_q15(n);
        std::vector<uint32_t> words(n);
        std::vector<int16_t> samples(2 * n);
        std::vector<std::complex<double>> reference(n);
        uint32_t seed = 12345;
        const auto gaussian = [&] {
            // Sum of 12 uniforms minus 6: close enough to N(0, 1).
            double sum = 0;

            for (int i = 0; i < 12; i++)
            {
                seed = seed * 1664525u + 1013904223u;
                sum += (seed >> 8) / 16777216.0;
            }

            return sum - 6.0;
        };

        for (const double sigma : {0.5, 1.0, 2.0, 4.0, 50.0})
        {
            double fixed = 0, exact = 0;

            for (int trial = 0; trial < 8; trial++)
            {
                std::vector<std::complex<double>> v(n);
                std::complex<double> mean;

                for (unsigned j = 0; j < n; j++)
                {
                    const auto lo = static_cast<int32_t>(std::lround(sigma * gaussian()));
                    const auto hi = static_cast<int32_t>(std::lround(sigma * gaussian()));

                    words[j] = (static_cast<uint32_t>(lo) & 0x3ffu) | ((static_cast<uint32_t>(hi) & 0x3ffu) << 10);
                    v[j] = {double(lo), -double(hi)};
                    mean += v[j];
                }

                mean /= n;

                const auto block = core::prepare_iq(words.data(), n, window.data(), fft.order(), samples.data());
                const double unscale = std::pow(4.0, -double(block.scale));

                fft.forward_permuted(samples.data());

                for (unsigned k = 0; k < n; k += 7) // a subset of bins keeps the DFT cheap
                {
                    std::complex<double> sum;

                    for (unsigned j = 0; j < n; j++)
                        sum += (v[j] - mean) * (window[j] / 32768.0) * std::polar(1.0, -2 * pi * k * j / n);

                    sum /= n;
                    exact += std::norm(sum);
                    fixed += (double(samples[2 * k]) * samples[2 * k] + double(samples[2 * k + 1]) * samples[2 * k + 1]) *
                             unscale;
                }
            }

            const double error_db = 10 * std::log10(fixed / exact);

            std::printf("noise floor at %.1f LSB rms: fixed-point %+.2f dB vs exact\n", sigma, error_db);
            CHECK(std::abs(error_db) < 0.2);
        }
    }

    // A capture scaled by 2^scale must land on the same normalized power as
    // the same signal at a different amplitude: codes differ by exactly the
    // amplitude ratio (12 codes = 6 dB for 2x).
    void test_block_scale_normalization()
    {
        const unsigned n = core::fft_size;
        const core::fft_q15 fft(core::fft_log2);
        const auto window = core::hann_q15(n);
        std::vector<uint32_t> words(n);
        std::vector<int16_t> samples(2 * n);
        uint8_t codes[2];
        unsigned scales[2];

        for (int i = 0; i < 2; i++)
        {
            const double amplitude = i ? 200.0 : 100.0;

            for (unsigned j = 0; j < n; j++)
                words[j] = dump_word(2 * pi * 5e6 * j / core::sample_rate_hz, amplitude);

            const auto block = core::prepare_iq(words.data(), n, window.data(), fft.order(), samples.data());
            core::line_accumulator line;
            std::vector<uint8_t> peak(320), mean(320);

            fft.forward_permuted(samples.data());
            line.configure(n, core::span_bins(40));
            line.add(samples.data(), block.scale);
            line.finish(peak.data(), mean.data(), 320);
            codes[i] = *std::max_element(peak.begin(), peak.end());
            scales[i] = block.scale;
        }

        std::printf("block scale: codes %u (scale %u) and %u (scale %u)\n", codes[0], scales[0], codes[1], scales[1]);
        CHECK(scales[0] == scales[1] + 1);
        CHECK(codes[1] >= codes[0] + 11 && codes[1] <= codes[0] + 13);
        CHECK(codes[1] < 255);
    }

    void test_power_code()
    {
        CHECK(core::power_code(0) == 0);
        CHECK(core::power_code(1) == 0);

        double worst = 0;

        for (double p = 2; p < 4e12; p *= 1.37)
        {
            const auto power = static_cast<uint64_t>(p);
            const double expected = 20 * std::log10(static_cast<double>(power));
            const auto code = core::power_code(power);

            if (expected < 254)
                worst = std::max(worst, std::abs(code - expected));
        }

        std::printf("power_code: worst error %.2f codes\n", worst);
        CHECK(worst <= 0.6);
        CHECK(core::power_code(UINT64_MAX) == 255);
    }

    // A tone above the LO must appear above the centre of the line.
    void test_orientation_and_line()
    {
        const unsigned n = core::fft_size;
        const core::fft_q15 fft(core::fft_log2);
        const auto window = core::hann_q15(n);
        std::vector<uint32_t> words(n);
        std::vector<int16_t> samples(2 * n);

        // +10 MHz at 80 MS/s, with a DC offset that prepare_iq removes.
        const double offset_hz = 10e6;

        for (unsigned j = 0; j < n; j++)
        {
            const uint32_t w = dump_word(2 * pi * offset_hz * j / core::sample_rate_hz, 300);
            const uint32_t low = ((w & 0x3ffu) + 40) & 0x3ffu, high = (((w >> 10) & 0x3ffu) - 25) & 0x3ffu;

            words[j] = low | (high << 10) | (55u << 20);
        }

        const auto block = core::prepare_iq(words.data(), n, window.data(), fft.order(), samples.data());

        CHECK(block.peak >= 299 && block.peak <= 341); // 300 plus the injected offset
        fft.forward_permuted(samples.data());

        const unsigned bin = argmax_power(samples.data(), n);

        CHECK(bin == 128); // 10 MHz / 78.125 kHz

        core::line_accumulator line;
        std::vector<uint8_t> peak(320), mean(320);

        line.configure(n, core::span_bins(40));
        CHECK(core::span_bins(40) == 512);
        line.add(samples.data(), block.scale);
        line.add(samples.data(), block.scale);
        CHECK(line.count() == 2);
        line.finish(peak.data(), mean.data(), 320);
        CHECK(line.count() == 0);

        unsigned best = 0;

        for (unsigned x = 0; x < 320; x++)
            if (peak[x] > peak[best])
                best = x;

        // Centre pixel 160 plus 10 MHz at 8 px/MHz.
        std::printf("orientation: tone at pixel %u (expected 240), DC pixel code %u\n", best, peak[160]);
        CHECK(best >= 239 && best <= 241);
        CHECK(peak[best] == mean[best]);
        CHECK(peak[160] + 40 < peak[best]); // DC removed
    }

    void test_floor_tracker()
    {
        core::floor_tracker floor;
        std::vector<uint8_t> codes(320, 50);

        for (unsigned x = 0; x < 40; x++)
            codes[x] = 120;

        floor.update(codes.data(), codes.size());
        CHECK(floor.floor() == 50);

        std::fill(codes.begin(), codes.end(), 70);

        for (int i = 0; i < 100; i++)
            floor.update(codes.data(), codes.size());

        CHECK(floor.floor() >= 69 && floor.floor() <= 70);
    }

    void test_channels()
    {
        CHECK(core::channels(core::band::ghz24).size() == 14);
        CHECK(core::channels(core::band::ghz24)[13].mhz == 2484);
        CHECK(core::channels(core::band::ghz5).front().mhz == 5180);
        CHECK(core::channels(core::band::ghz5).back().mhz == 5885);
        CHECK(core::channels_5[20].number == 149 && core::channels_5[20].mhz == 5745);

        core::state s;

        CHECK(s.current_mhz() == 2437);
        s.band = core::band::ghz5;
        CHECK(s.current_mhz() == 5180);
    }

    void test_tuning()
    {
        using core::band;

        CHECK(core::range_of(band::ghz24).low_mhz == 2130 && core::range_of(band::ghz24).high_mhz == 2730);
        CHECK(core::range_of(band::ghz5).low_mhz == 4790 && core::range_of(band::ghz5).high_mhz == 5990);
        CHECK(core::step_frequency(band::ghz24, 2437, 5) == 2442);
        CHECK(core::step_frequency(band::ghz24, 2437, -20) == 2417);
        CHECK(core::step_frequency(band::ghz24, 2730, 1) == 2130);  // wraps up
        CHECK(core::step_frequency(band::ghz24, 2132, -5) == 2730); // wraps down
        CHECK(core::step_frequency(band::ghz5, 5985, 10) == 4790);
        CHECK(core::step_frequency(band::ghz5, 4790, 0) == 4790);
    }

    struct press
    {
        uint32_t from, to;
        bool a, b;
    };

    std::vector<core::gesture> run(std::initializer_list<press> presses, uint32_t end_ms)
    {
        core::gesture_decoder decoder;
        std::vector<core::gesture> out;

        for (uint32_t t = 0; t <= end_ms; t += 10)
        {
            bool a = false, b = false;

            for (const auto &p : presses)
                if (t >= p.from && t < p.to)
                {
                    a |= p.a;
                    b |= p.b;
                }

            if (const auto g = decoder.update(t, a, b); g != core::gesture::none)
                out.push_back(g);
        }

        return out;
    }

    void test_gestures()
    {
        using g = core::gesture;

        CHECK(run({{100, 250, true, false}}, 1000) == std::vector<g>{g::short_a});
        CHECK(run({{100, 250, false, true}}, 1000) == std::vector<g>{g::short_b});
        CHECK(run({{100, 1500, true, false}}, 2000) == std::vector<g>{g::long_a});
        CHECK(run({{100, 1500, false, true}}, 2000) == std::vector<g>{g::long_b});
        // Both, pressed a little apart and released apart: one event.
        CHECK(run({{100, 600, true, false}, {150, 900, false, true}}, 1500) == std::vector<g>{g::both});
        // Bounces shorter than the debounce time are ignored.
        CHECK(run({{100, 115, true, false}, {130, 140, true, false}}, 500).empty());
        // Two separate clicks.
        CHECK(run({{100, 200, true, false}, {400, 500, true, false}}, 1000) == std::vector<g>{g::short_a, g::short_a});
    }

    void test_auto_level()
    {
        core::auto_level level;

        // Clipping steps down at once, to zero at most.
        CHECK(level.update(60, 80, 511) == 56);
        CHECK(level.update(2, 80, 500) == 0);
        // Mid-level signals hold the gain.
        for (int i = 0; i < 200; i++)
            CHECK(level.update(40, 80, 300) == 40);
        // Quiet for release_lines lines: one release step, capped at max.
        unsigned g = 40;

        for (unsigned i = 0; i < core::auto_level::release_lines; i++)
            g = level.update(g, 80, 50);

        CHECK(g == 42);
        CHECK(level.update(200, 80, 300) == 80);

        // Quiet lines interrupted by a clip start over.
        level.reset();
        g = 40;

        for (unsigned i = 0; i + 1 < core::auto_level::release_lines; i++)
            g = level.update(g, 80, 50);

        g = level.update(g, 80, 490);
        CHECK(g == 36);

        for (unsigned i = 0; i + 1 < core::auto_level::release_lines; i++)
            g = level.update(g, 80, 50);

        CHECK(g == 36);
    }

    void test_menu()
    {
        core::state s;
        char value[24];

        core::menu_value(s, core::menu_item::view, value, sizeof(value), nullptr);
        CHECK(std::string(value) == "Detail");
        CHECK(std::string(core::menu_label(core::menu_item::version)) == "Version");
        CHECK(core::menu_change(s, core::menu_item::view, 80) && s.view == core::view_id::band);
        CHECK(!core::menu_change(s, core::menu_item::span, 80) && s.span_mhz == 40);
        core::menu_value(s, core::menu_item::span, value, sizeof(value), nullptr);
        CHECK(std::string(value) == "Whole band");
        CHECK(core::menu_change(s, core::menu_item::view, 80) && s.view == core::view_id::detail);
        CHECK(core::menu_change(s, core::menu_item::span, 80) && s.span_mhz == 80);
        CHECK(core::menu_change(s, core::menu_item::span, 80) && s.span_mhz == 20);

        CHECK(s.step_mhz == 5);
        CHECK(core::menu_change(s, core::menu_item::step, 80) && s.step_mhz == 10);
        core::menu_value(s, core::menu_item::step, value, sizeof(value), nullptr);
        CHECK(std::string(value) == "10 MHz");
        core::menu_change(s, core::menu_item::step, 80);
        core::menu_change(s, core::menu_item::step, 80);
        CHECK(s.step_mhz == 1);

        CHECK(s.gain == core::gain_auto);
        core::menu_value(s, core::menu_item::gain, value, sizeof(value), nullptr);
        CHECK(std::string(value) == "Auto");
        core::menu_change(s, core::menu_item::gain, 20);
        CHECK(s.gain == core::gain_agc);
        core::menu_change(s, core::menu_item::gain, 20);
        CHECK(s.gain == 8);
        core::menu_change(s, core::menu_item::gain, 20);
        CHECK(s.gain == 16);
        core::menu_change(s, core::menu_item::gain, 20);
        CHECK(s.gain == core::gain_auto);

        s.brightness = 100;
        core::menu_change(s, core::menu_item::brightness, 80);
        CHECK(s.brightness == 20);
        s.brightness = 55; // not a level: restart from the first
        core::menu_change(s, core::menu_item::brightness, 80);
        CHECK(s.brightness == 20);

        CHECK(!core::menu_change(s, core::menu_item::version, 80));
        core::menu_value(s, core::menu_item::version, value, sizeof(value), "v1.2.3");
        CHECK(std::string(value) == "v1.2.3");

        for (auto item : core::menu_items)
            CHECK(core::menu_label(item)[0] != '\0');
    }

    void test_survey_plan()
    {
        const auto p24 = core::make_survey_plan(core::band::ghz24);
        const auto p5 = core::make_survey_plan(core::band::ghz5);

        CHECK(p24.centres_mhz.size() == 15);
        CHECK(p24.centres_mhz.front() == 2150 && p24.centres_mhz.back() == 2710);
        CHECK(p5.centres_mhz.size() == 30);
        CHECK(p5.centres_mhz.front() == 4810 && p5.centres_mhz.back() == 5970);
        CHECK(core::survey_pixel(p24, 320, 2130) == 0);
        CHECK(core::survey_pixel(p24, 320, 2430) == 160);
        CHECK(core::survey_pixel(p24, 320, 2730) == -1);
        // Every Wi-Fi channel is on its band's line.
        for (const auto &c : core::channels_24)
            CHECK(core::survey_pixel(p24, 320, c.mhz) >= 0);
        for (const auto &c : core::channels_5)
            CHECK(core::survey_pixel(p5, 320, c.mhz) >= 0);
    }

    // A tone 5 MHz above the second step's centre lands on its pixel; every
    // pixel of the line is covered by some step.
    void test_survey_accumulator()
    {
        const unsigned n = core::fft_size;
        const core::fft_q15 fft(core::fft_log2);
        const auto window = core::hann_q15(n);
        const auto plan = core::make_survey_plan(core::band::ghz24);
        std::vector<uint32_t> words(n), quiet(n, 0);
        std::vector<int16_t> samples(2 * n), silence(2 * n, 0);
        core::survey_accumulator line;
        std::vector<uint8_t> peak(320), mean(320);

        for (unsigned j = 0; j < n; j++)
            words[j] = dump_word(2 * pi * 5e6 * j / core::sample_rate_hz, 200);

        line.configure(plan, n, core::sample_rate_hz, 320);

        for (unsigned step = 0; step < plan.centres_mhz.size(); step++)
        {
            const auto block = core::prepare_iq(step == 1 ? words.data() : quiet.data(), n, window.data(), fft.order(),
                                                samples.data());

            fft.forward_permuted(samples.data());
            line.add(samples.data(), block.scale, step);
        }

        CHECK(line.captures() == plan.centres_mhz.size());
        line.finish(peak.data(), mean.data());

        unsigned best = 0;

        for (unsigned x = 0; x < 320; x++)
            if (peak[x] > peak[best])
                best = x;

        const int expected = core::survey_pixel(plan, 320, plan.centres_mhz[1] + 5);

        std::printf("survey: tone at pixel %u (expected %d)\n", best, expected);
        CHECK(std::abs(static_cast<int>(best) - expected) <= 1);
        CHECK(line.captures() == 0);
    }

    // Every step sees the same passband hump; the survey must learn and remove
    // it, while a signal present in one step only stays visible.
    void test_survey_passband()
    {
        const unsigned n = core::fft_size;
        const auto plan = core::make_survey_plan(core::band::ghz24);
        core::survey_accumulator line;
        std::vector<int16_t> spectrum(2 * n);
        std::vector<uint8_t> peak(320), mean(320);
        uint32_t seed = 7;
        const auto uniform = [&] {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) / 16777216.0;
        };
        const int tone_px = core::survey_pixel(plan, 320, plan.centres_mhz[1] + 11);

        line.configure(plan, n, core::sample_rate_hz, 320);

        for (int l = 0; l < 60; l++)
        {
            for (unsigned step = 0; step < plan.centres_mhz.size(); step++)
                for (int c = 0; c < 6; c++)
                {
                    std::fill(spectrum.begin(), spectrum.end(), 0);

                    for (unsigned k = 0; k < n; k++)
                    {
                        const int offset = static_cast<int>(k < n / 2 ? k : k - n); // bins from DC
                        const double hump_db = -10.0 * (offset / 256.0) * (offset / 256.0);
                        double power = 400.0 * std::pow(10.0, hump_db / 10) * (0.5 + uniform());

                        // A 3 MHz wide signal from +10 MHz in the second step, 20 dB up.
                        if (step == 1 && offset >= 128 && offset < 168)
                            power *= 100;

                        spectrum[2 * k] = static_cast<int16_t>(std::lround(std::sqrt(power)));
                    }

                    line.add(spectrum.data(), core::max_scale - 4, step); // x256: realistic levels
                }

            line.finish(peak.data(), mean.data());
        }

        // Ignore the tone and a pixel either side of it.
        uint8_t lo = 255, hi = 0;

        for (int x = 0; x < 320; x++)
            if (std::abs(x - tone_px) > 1)
            {
                lo = std::min(lo, mean[x]);
                hi = std::max(hi, mean[x]);
            }

        std::printf("survey passband: flat floor %u..%u codes, tone %u codes\n", lo, hi, mean[tone_px]);
        CHECK(hi - lo <= 4);                  // within 2 dB after correction (the hump was 10 dB)
        // The 20 dB signal fills its pixel; it must stay at least 15 dB clear of
        // the corrected floor (the passband estimate must not absorb it).
        CHECK(mean[tone_px] >= hi + 30);
        CHECK(*std::min_element(mean.begin(), mean.end()) > 0); // guard pixels were filled
    }

    void test_exp2_log2()
    {
        CHECK(core::exp2_q8(0) == 256);
        CHECK(core::exp2_q8(256) == 512);
        CHECK(core::exp2_q8(-256) == 128);
        CHECK(core::log2_q8(1024) == 10 * 256);

        // Largest results that fit, then saturation instead of wrapping.
        CHECK(core::exp2_q8(22 * 256) == (1u << 30));
        CHECK(core::exp2_q8(23 * 256 - 1) > (1u << 30));
        CHECK(core::exp2_q8(23 * 256) == UINT32_MAX);
        CHECK(core::exp2_q8(-40 * 256) == 0);

        for (int32_t x = -1024; x <= 1024; x += 37)
        {
            const double expected = 256.0 * std::exp2(x / 256.0);

            CHECK(std::abs(core::exp2_q8(x) - expected) <= 1.0);
        }
    }

    bool same_settings(const core::state &a, const core::state &b)
    {
        return a.view == b.view && a.band == b.band && a.frequency_mhz == b.frequency_mhz && a.span_mhz == b.span_mhz &&
               a.step_mhz == b.step_mhz && a.detector == b.detector && a.colormap == b.colormap &&
               a.brightness == b.brightness && a.gain == b.gain;
    }

    void test_settings_codec()
    {
        namespace codec = core::settings_codec;
        const core::state defaults;

        // Round trip with every field away from its default.
        core::state s;

        s.view = core::view_id::band;
        s.band = core::band::ghz5;
        s.frequency_mhz = {2350, 5805};
        s.span_mhz = 80;
        s.step_mhz = 20;
        s.detector = core::detector::mean;
        s.colormap = core::colormap_id::grayscale;
        s.brightness = 40;
        s.gain = 32;
        s.frozen = true; // not persisted

        const auto blob = codec::encode(s);
        const auto back = codec::decode(blob);

        CHECK(blob.size() == 15);
        CHECK(back && same_settings(*back, s) && !back->frozen);

        // Byte-identical to the struct the firmware wrote before the codec
        // existed (main/settings.cpp up to 3e5e724), so saved settings survive.
        struct legacy
        {
            uint8_t version, band, channel_index[2], span_mhz, detector, colormap, brightness, gain, view;
            uint8_t frequency[2][2];
            uint8_t step_mhz;
        } const old = {4, 1, {0, 0}, 80, 1, 1, 40, 32, 1, {{2350 & 255, 2350 >> 8}, {5805 & 255, 5805 >> 8}}, 20};

        static_assert(sizeof(legacy) == codec::size);
        CHECK(std::memcmp(&old, blob.data(), sizeof(old)) == 0);

        // Version 1 (8 bytes): Wi-Fi channel indexes become frequencies.
        const uint8_t v1[] = {1, 1, 5, 20, 80, 1, 1, 60};
        const auto from_v1 = codec::decode(v1);

        CHECK(from_v1.has_value());
        CHECK(from_v1->band == core::band::ghz5);
        CHECK(from_v1->frequency_mhz[0] == 2437 && from_v1->frequency_mhz[1] == 5745); // ch 6, ch 149
        CHECK(from_v1->span_mhz == 80 && from_v1->detector == core::detector::mean);
        CHECK(from_v1->colormap == core::colormap_id::grayscale && from_v1->brightness == 60);
        CHECK(from_v1->gain == defaults.gain && from_v1->view == defaults.view && from_v1->step_mhz == defaults.step_mhz);

        // Versions 2 and 3 add gain, then view.
        const uint8_t v2[] = {2, 0, 13, 0, 20, 0, 0, 80, 16};
        const uint8_t v3[] = {3, 0, 13, 0, 20, 0, 0, 80, 16, 1};

        CHECK(codec::decode(v2) && codec::decode(v2)->gain == 16 && codec::decode(v2)->frequency_mhz[0] == 2484);
        CHECK(codec::decode(v3) && codec::decode(v3)->view == core::view_id::band);

        // Unknown versions and sizes are rejected; nothing is half-applied.
        const uint8_t future[16] = {5};
        const uint8_t v4_short[10] = {4};
        const uint8_t v0[8] = {0};

        CHECK(!codec::decode({}));
        CHECK(!codec::decode(future));
        CHECK(!codec::decode(v4_short));
        CHECK(!codec::decode(v0));
        CHECK(!codec::decode(std::span(v1, 7)));

        // Out-of-range values keep their defaults; brightness is clamped.
        auto bad = codec::encode(s);

        bad[10] = 2000 & 255; // 2000 MHz: below the 2.4G range
        bad[11] = 2000 >> 8;
        bad[4] = 30;  // not a span
        bad[14] = 3;  // not a step
        bad[7] = 250; // brightness

        const auto sanitised = codec::decode(bad);

        CHECK(sanitised.has_value());
        CHECK(sanitised->frequency_mhz[0] == defaults.frequency_mhz[0] && sanitised->frequency_mhz[1] == 5805);
        CHECK(sanitised->span_mhz == defaults.span_mhz && sanitised->step_mhz == defaults.step_mhz);
        CHECK(sanitised->brightness == 100);

        const uint8_t v1_bad_channel[] = {1, 0, 99, 99, 40, 0, 0, 0};
        const auto from_bad = codec::decode(v1_bad_channel);

        CHECK(from_bad && from_bad->frequency_mhz == defaults.frequency_mhz && from_bad->brightness == 10);
    }

    void test_colormap()
    {
        // Turbo is blue at a quarter and red at the top; values are byte-swapped RGB565.
        const auto unswap = [](uint16_t v) { return static_cast<uint16_t>((v >> 8) | (v << 8)); };
        const uint16_t low = unswap(core::turbo[64]), last = unswap(core::turbo[255]);

        CHECK((low & 0x1f) > (low >> 11));
        CHECK((last >> 11) > (last & 0x1f));
        CHECK(core::rgb565(255, 0, 0) == 0x00f8);
    }
}

int main()
{
    test_fft_matches_dft();
    test_fft_full_scale_does_not_wrap();
    test_noise_floor_precision();
    test_block_scale_normalization();
    test_power_code();
    test_orientation_and_line();
    test_floor_tracker();
    test_channels();
    test_tuning();
    test_gestures();
    test_auto_level();
    test_menu();
    test_survey_plan();
    test_survey_accumulator();
    test_survey_passband();
    test_exp2_log2();
    test_settings_codec();
    test_colormap();

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);

    return failures ? 1 : 0;
}
