// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for components/core (no ESP-IDF).
#include <cmath>
#include <complex>
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
#include "core/spectrum.hpp"
#include "core/state.hpp"

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

    void test_fft_matches_dft()
    {
        constexpr unsigned log2n = 8, n = 1u << log2n;
        const core::fft_q15 fft(log2n);
        std::vector<int16_t> x(2 * n);
        std::vector<std::complex<double>> input(n);

        // Two tones plus an offset.
        for (unsigned j = 0; j < n; j++)
        {
            input[j] = 9000.0 * std::polar(1.0, 2 * pi * 13 * j / n) + 4000.0 * std::polar(1.0, -2 * pi * 70.5 * j / n) +
                       std::complex<double>(1200, -800);
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

        std::printf("fft: worst bin error %.2f LSB\n", worst);
        CHECK(worst < 4.0);
    }

    void test_fft_full_scale_does_not_wrap()
    {
        constexpr unsigned log2n = 10, n = 1u << log2n;
        const core::fft_q15 fft(log2n);
        std::vector<int16_t> x(2 * n);

        for (unsigned j = 0; j < n; j++)
        {
            x[2 * j] = 32767;
            x[2 * j + 1] = -32767;
        }

        fft.forward(x.data());
        CHECK(x[0] > 32000 && x[1] < -32000);
        CHECK(argmax_power(x.data(), n) == 0);
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

        core::prepare_iq(words.data(), n, window.data(), samples.data());
        fft.forward(samples.data());

        const unsigned bin = argmax_power(samples.data(), n);

        CHECK(bin == 128); // 10 MHz / 78.125 kHz

        core::line_accumulator line;
        std::vector<uint8_t> peak(320), mean(320);

        line.configure(n, core::span_bins(40));
        CHECK(core::span_bins(40) == 512);
        line.add(samples.data());
        line.add(samples.data());
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
        CHECK(core::step_channel(core::band::ghz24, 0, -1) == 13);
        CHECK(core::step_channel(core::band::ghz24, 13, 1) == 0);
        CHECK(core::step_channel(core::band::ghz5, 27, 1) == 0);
        CHECK(core::step_channel(core::band::ghz5, 3, 2) == 5);

        core::state s;

        CHECK(s.current_channel().number == 6);
        s.band = core::band::ghz5;
        CHECK(s.current_channel().number == 36);
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

        CHECK(core::menu_change(s, core::menu_item::span, 80) && s.span_mhz == 80);
        CHECK(core::menu_change(s, core::menu_item::span, 80) && s.span_mhz == 20);

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

        CHECK(!core::menu_change(s, core::menu_item::about, 80));
        core::menu_value(s, core::menu_item::about, value, sizeof(value), "v1.2.3");
        CHECK(std::string(value) == "v1.2.3");

        for (auto item : core::menu_items)
            CHECK(core::menu_label(item)[0] != '\0');
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
    test_power_code();
    test_orientation_and_line();
    test_floor_tracker();
    test_channels();
    test_gestures();
    test_auto_level();
    test_menu();
    test_colormap();

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);

    return failures ? 1 : 0;
}
