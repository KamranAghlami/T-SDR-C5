// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sdkconfig.h>

// The receiver of the board being built, behind one interface: c5_radio on
// the T-Display-C5, s3_radio on the T-Display-S3. Both capture IQ10 words in
// the same layout at 80 MS/s.
#if CONFIG_IDF_TARGET_ESP32C5
#include "c5_radio.h"
#define RADIO_FN(name) c5_radio_##name
#elif CONFIG_IDF_TARGET_ESP32S3
#include "s3_radio.h"
#define RADIO_FN(name) s3_radio_##name
#else
#error "No receiver for this target"
#endif

namespace radio
{
#if CONFIG_IDF_TARGET_ESP32C5
    using stats = c5_radio_stats_t;
    inline constexpr auto rate_80m = C5_RATE_80M;
#else
    using stats = s3_radio_stats_t;
    inline constexpr auto rate_80m = S3_RATE_80M;
#endif

    inline esp_err_t init() { return RADIO_FN(init)(); }
    inline esp_err_t tune(unsigned mhz) { return RADIO_FN(tune)(mhz); }
    inline esp_err_t tune_fast(unsigned mhz) { return RADIO_FN(tune_fast)(mhz); }
    inline void gain_hardware() { RADIO_FN(gain_hardware)(); }
    inline void gain_manual(unsigned index) { RADIO_FN(gain_manual)(index); }
    inline unsigned gain_max() { return RADIO_FN(gain_max)(); }
    inline esp_err_t bandwidth(unsigned mhz) { return RADIO_FN(bandwidth)(mhz); }
    inline void get_stats(stats *out) { RADIO_FN(get_stats)(out); }

    inline esp_err_t capture(unsigned n, const uint32_t **words) { return RADIO_FN(capture)(n, rate_80m, words, nullptr); }
}

#undef RADIO_FN
