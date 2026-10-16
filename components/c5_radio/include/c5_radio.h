/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ESP32-C5 receive-only SDR front end, built on ESP-SDR's C5 receiver
 * (third_party/esp-sdr). One task owns the radio; none of this is thread-safe. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dump-engine divider codes: 80/40/20/10/8/4 MS/s. Lower rates drop samples
 * without an anti-alias filter, so narrow the analog bandwidth with them. */
typedef enum {
    C5_RATE_80M = 0,
    C5_RATE_40M = 1,
    C5_RATE_20M = 2,
    C5_RATE_10M = 3,
    C5_RATE_8M = 4,
    C5_RATE_4M = 5,
} c5_rate_t;

#define C5_RADIO_MAX_SAMPLES 16380u

/* Wi-Fi in NULL mode + promiscuous, PHY calibrated, receiver at 2412 MHz. */
esp_err_t c5_radio_init(void);

/* 100..6000 MHz attempt range; measures fresh RX DC at the new LO. */
esp_err_t c5_radio_tune(unsigned mhz);
unsigned c5_radio_frequency(void);

/* Retune without measuring DC at the new LO (band survey steps). Faster than
 * c5_radio_tune(); use c5_radio_tune() again to return to one channel. */
esp_err_t c5_radio_tune_fast(unsigned mhz);

/* Hardware AGC (default), or a fixed PHY gain-table index (not dB). */
void c5_radio_gain_hardware(void);
void c5_radio_gain_manual(unsigned index);
unsigned c5_radio_gain_max(void);

/* Approximate two-sided analog bandwidth in MHz (11..48); 0 = widest. */
esp_err_t c5_radio_bandwidth(unsigned mhz);

typedef struct {
    uint32_t captures;       /* successful snapshots */
    uint32_t failures;       /* timeouts and overruns */
    uint32_t recalibrations; /* RX DC measurements */
} c5_radio_stats_t;

/* Snapshot of n complex samples. On success *words points to n IQ10 words
 * that stay valid until the next capture: I in bits 0..9, Q in bits 10..19
 * (signed), PHY gain index in bits 20..26 of words[0]. */
esp_err_t c5_radio_capture(unsigned n, c5_rate_t rate, const uint32_t **words, uint32_t *elapsed_us);

/* Cumulative counters since boot. */
void c5_radio_get_stats(c5_radio_stats_t *stats);

#ifdef __cplusplus
}
#endif
