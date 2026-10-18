/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ESP32-S3 receive-only SDR front end, built on ESP-SDR's S3 receiver
 * (third_party/esp-sdr). One task owns the radio; none of this is thread-safe. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native dump clocks of the S3 (no software resampling). */
typedef enum {
    S3_RATE_80M = 0,
    S3_RATE_40M = 1,
    S3_RATE_16M = 6,
} s3_rate_t;

#define S3_RADIO_MAX_SAMPLES 16380u

/* Wi-Fi in NULL mode + promiscuous, PHY calibrated, receiver at 2412 MHz. */
esp_err_t s3_radio_init(void);

/* 100..6000 MHz attempt range; measures fresh RX DC at the new LO. */
esp_err_t s3_radio_tune(unsigned mhz);
unsigned s3_radio_frequency(void);

/* Retune without measuring DC at the new LO (band survey steps). Faster than
 * s3_radio_tune(); use s3_radio_tune() again to stay on one frequency. */
esp_err_t s3_radio_tune_fast(unsigned mhz);

/* Hardware AGC (default), or a fixed PHY gain-table index (not dB). */
void s3_radio_gain_hardware(void);
void s3_radio_gain_manual(unsigned index);
unsigned s3_radio_gain_max(void);

/* Approximate two-sided analog bandwidth in MHz; 0 = widest. */
esp_err_t s3_radio_bandwidth(unsigned mhz);

typedef struct {
    uint32_t captures;       /* successful snapshots */
    uint32_t failures;       /* timeouts */
    uint32_t recalibrations; /* RX DC measurements */
} s3_radio_stats_t;

/* Snapshot of n complex samples. On success *words points to n IQ10 words
 * that stay valid until the next capture: I in bits 0..9, Q in bits 10..19
 * (signed). */
esp_err_t s3_radio_capture(unsigned n, s3_rate_t rate, const uint32_t **words, uint32_t *elapsed_us);

/* Cumulative counters since boot. */
void s3_radio_get_stats(s3_radio_stats_t *stats);

#ifdef __cplusplus
}
#endif
