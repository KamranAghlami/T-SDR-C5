// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace core
{
    // RGB565 with the bytes swapped, the order the ST7789 expects over SPI.
    constexpr uint16_t rgb565(unsigned r, unsigned g, unsigned b)
    {
        const uint16_t c = static_cast<uint16_t>(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));

        return static_cast<uint16_t>((c >> 8) | (c << 8));
    }

    namespace detail
    {
        constexpr double poly(double x, const double (&c)[6])
        {
            return c[0] + x * (c[1] + x * (c[2] + x * (c[3] + x * (c[4] + x * c[5]))));
        }

        constexpr unsigned unit_to_8bit(double v)
        {
            return v <= 0.0 ? 0u : v >= 1.0 ? 255u : static_cast<unsigned>(v * 255.0 + 0.5);
        }
    }

    // Google's "Turbo" colormap (polynomial approximation, Apache-2.0).
    inline constexpr std::array<uint16_t, 256> turbo = [] {
        constexpr double r[6] = {0.13572138, 4.61539260, -42.66032258, 132.13108234, -152.94239396, 59.28637943};
        constexpr double g[6] = {0.09140261, 2.19418839, 4.84296658, -14.18503333, 4.27729857, 2.82956604};
        constexpr double b[6] = {0.10667330, 12.64194608, -60.58204836, 110.36276771, -89.90310912, 27.34824973};
        std::array<uint16_t, 256> lut{};

        for (unsigned i = 0; i < 256; i++)
        {
            const double x = i / 255.0;

            lut[i] = rgb565(detail::unit_to_8bit(detail::poly(x, r)), detail::unit_to_8bit(detail::poly(x, g)),
                            detail::unit_to_8bit(detail::poly(x, b)));
        }

        return lut;
    }();

    inline constexpr std::array<uint16_t, 256> grayscale = [] {
        std::array<uint16_t, 256> lut{};

        for (unsigned i = 0; i < 256; i++)
            lut[i] = rgb565(i, i, i);

        return lut;
    }();
}
