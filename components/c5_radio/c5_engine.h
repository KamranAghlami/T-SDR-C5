/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Lower 64 KiB half of the dump bank minus four canary words. */
#define C5_ENGINE_MAX_SAMPLES 16380u

/* One finite dump of n samples at the given divider (0..5 = 80..4 MS/s) into
 * SRAM bank 1. The receiver must already be prepared; the caller applies and
 * restores the analog filter around the call. */
esp_err_t c5_engine_capture(unsigned n, unsigned divider, const uint32_t **words, uint32_t *elapsed_us);
