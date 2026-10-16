/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Receive setup and snapshot capture for the ESP32-C5, adapted from ESP-SDR's
 * shared C5/C6/C61 receiver (third_party/esp-sdr/main/families/c5_c6_c61/
 * receiver.c, Copyright ESP-SDR contributors / ESPARGOS). The serial protocol
 * is dropped; the PHY sequence, analog filter handling, gain control and
 * capture sentinels follow upstream. The 5 GHz Wi-Fi-driver tuning path
 * follows ESPsoup's TUNEM 2 (main/targets/esp32c5/tuning.h, GPL-3.0-or-later). */
#include <string.h>
#include "sdkconfig.h"
#include "c5_radio.h"
#include "c5_engine.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "heap_memory_layout.h"
#include "nvs_flash.h"
#include "soc/soc.h"

/* Upstream C5 target: SRAM_OWNER_REG, IQ_BUFFER, the bank-1 heap reservation,
 * stock_capture() (vendor adctrig) and c5_set_chan(). */
#include "chip.h"
#include "rx_bandwidth.h"
#include "rx_recalibration.h"

extern void phy_stop_tx_tone(unsigned);
extern void phy_pbus_workmode(void);
extern void phy_pbus_xpd_rx_on(unsigned);
extern void phy_pbus_xpd_tx_off(void);
extern void phy_set_rxclk_en(unsigned);
extern unsigned phy_chip_i2c_readReg(unsigned, unsigned, unsigned);
extern void phy_i2c_writeReg(unsigned, unsigned, unsigned, unsigned);
extern void force_rx_gain(unsigned, unsigned, unsigned);
extern void phy_change_channel(int mhz, int a, int b, int c);

#define RX_FILTER_REG 6u
#define GAIN_REG 0x600a702cu
#define SENTINEL 0xa5a0055au
#define CANARY 0x5a5aa5a5u
/* A PHY that re-measures DC while we set up the channel would otherwise make
 * every capture recalibrate; between retunes, accept at most this rate. */
#define RECALIBRATE_INTERVAL_US 500000
#define TRY(x) do { esp_err_t err_ = (x); if (err_ != ESP_OK) return err_; } while (0)

/* Required by the stock RF test archive; no shell is exposed. */
int cmd_parse(char *cmd, char *name, int *argc, char **argv) {
    (void)cmd; (void)name; (void)argc; (void)argv;
    return -1;
}

static unsigned frequency_mhz = 2412;
static unsigned channel_mode = 1; /* PHY mode 1: wide path, matches BANDWIDTH 0 */
static int analog_filter = -1;    /* -1: PHY-calibrated capacitor code */
static bool rx_ready;
static bool gain_manual;
static unsigned gain_code = 40;
static c5_radio_stats_t stats;

unsigned c5_radio_gain_max(void) {
    unsigned maximum = (REG_READ(GAIN_REG) >> 8) & 127u;
    return maximum < 90u ? maximum : 0u; /* never expose uncalibrated slots */
}

static void gain_apply(void) {
    if (gain_code > c5_radio_gain_max()) gain_code = c5_radio_gain_max();
    force_rx_gain(gain_manual, gain_code, 0);
}

/* RX-only analog capacitance, applied per capture and restored afterwards so
 * PHY calibration stays intact across retunes. */
static void analog_apply(unsigned saved[2]) {
    for (unsigned j = 0; j < 2; j++) {
        saved[j] = phy_chip_i2c_readReg(0x67, 1, RX_FILTER_REG + j);
        if (analog_filter >= 0)
            phy_i2c_writeReg(0x67, 1, RX_FILTER_REG + j, (saved[j] & ~63u) | (unsigned)analog_filter);
    }
}

static void analog_restore(const unsigned saved[2]) {
    if (analog_filter >= 0)
        for (unsigned j = 0; j < 2; j++) phy_i2c_writeReg(0x67, 1, RX_FILTER_REG + j, saved[j]);
}

/* Above 3 GHz the stock path is about 20 dB less sensitive from ~5745 MHz
 * (ESPsoup, CW generator in 10 MHz steps). The Wi-Fi driver path tunes 4790 to
 * at least 5990 MHz exactly: select a 5 GHz channel through the driver, then
 * program the PLL in MHz. The PHY channel mode is not applied on this path;
 * the analog filter setting is. */
static void set_channel(unsigned mhz, unsigned mode) {
    if (mhz > 3000) {
        (void)esp_wifi_set_channel(140, WIFI_SECOND_CHAN_NONE);
        phy_change_channel((int)mhz, 1, 0, 0);
    } else {
        c5_set_chan(mhz, mode);
    }
}

static void prepare_rx(void) {
    static unsigned calibrated_mhz;
    static int64_t calibrated_at;
    bool stale = rx_recalibration_stale();
    int64_t now = esp_timer_get_time();
    if (rx_ready && (!stale || now - calibrated_at < RECALIBRATE_INTERVAL_US)) return;
    if (calibrated_mhz != frequency_mhz || stale) {
        rx_recalibrate(frequency_mhz);
        calibrated_mhz = frequency_mhz;
        calibrated_at = now;
        stats.recalibrations++;
    }
    set_channel(frequency_mhz, channel_mode);
    phy_stop_tx_tone(1);
    phy_pbus_workmode();
    phy_pbus_xpd_tx_off();
    phy_pbus_xpd_rx_on(1);
    phy_set_rxclk_en(1);
    gain_apply();
    rx_ready = true;
}

esp_err_t c5_radio_init(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    if (e != ESP_OK) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    TRY(esp_wifi_init(&cfg));
    TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    TRY(esp_wifi_set_mode(WIFI_MODE_NULL));
    TRY(esp_wifi_start());
    TRY(esp_wifi_set_ps(WIFI_PS_NONE));
    TRY(esp_wifi_set_promiscuous(true));
    TRY(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    prepare_rx();
    return ESP_OK;
}

esp_err_t c5_radio_tune(unsigned mhz) {
    if (!frequency_valid(mhz)) return ESP_ERR_INVALID_ARG;
    frequency_mhz = mhz;
    rx_ready = false;
    prepare_rx();
    return ESP_OK;
}

unsigned c5_radio_frequency(void) { return frequency_mhz; }

void c5_radio_gain_hardware(void) {
    gain_manual = false;
    gain_apply();
}

void c5_radio_gain_manual(unsigned index) {
    gain_manual = true;
    gain_code = index;
    gain_apply();
}

esp_err_t c5_radio_bandwidth(unsigned mhz) {
    if (mhz && (mhz < RX_BANDWIDTH_MIN || mhz > RX_BANDWIDTH_MAX)) return ESP_ERR_INVALID_ARG;
    analog_filter = rx_bandwidth_dcap(mhz);
    unsigned mode = rx_bandwidth_phy_mode(mhz);
    if (mode != channel_mode) {
        /* Mode changes need a complete channel setup (PBUS analog tables). */
        channel_mode = mode;
        rx_ready = false;
        prepare_rx();
    }
    return ESP_OK;
}

#ifdef CONFIG_C5_RADIO_STOCK_CAPTURE
/* Vendor adctrig path, as upstream; kept for comparisons. */
static esp_err_t stock_engine_capture(unsigned n, unsigned divider, const uint32_t **words, uint32_t *elapsed_us) {
    for (unsigned j = 0; j < n; j++) IQ_BUFFER[j] = SENTINEL;
    for (unsigned j = 0; j < 4; j++) IQ_BUFFER[n + j] = CANARY ^ j;
    uint32_t owner = REG_READ(SRAM_OWNER_REG);
    int64_t start = esp_timer_get_time();
    bool done = stock_capture(n, divider);
    if (elapsed_us) *elapsed_us = (uint32_t)(esp_timer_get_time() - start);
    REG_WRITE(SRAM_OWNER_REG, owner);
    if (!done) return ESP_ERR_TIMEOUT;
    for (unsigned j = 0; j < n; j++)
        if (IQ_BUFFER[j] == SENTINEL) return ESP_ERR_TIMEOUT;
    for (unsigned j = 0; j < 4; j++)
        if (IQ_BUFFER[n + j] != (CANARY ^ j)) return ESP_ERR_INVALID_SIZE;
    *words = IQ_BUFFER;
    return ESP_OK;
}
#define engine_capture stock_engine_capture
#else
#define engine_capture c5_engine_capture
#endif

esp_err_t c5_radio_capture(unsigned n, c5_rate_t rate, const uint32_t **words, uint32_t *elapsed_us) {
    if (n < 256 || n > C5_RADIO_MAX_SAMPLES || (unsigned)rate > C5_RATE_4M) return ESP_ERR_INVALID_ARG;
    prepare_rx();
    unsigned saved[2];
    analog_apply(saved);
    esp_err_t e = engine_capture(n, (unsigned)rate, words, elapsed_us);
    analog_restore(saved);
    if (e == ESP_OK) stats.captures++;
    else stats.failures++;
    return e;
}

void c5_radio_get_stats(c5_radio_stats_t *out) { *out = stats; }

#ifndef CONFIG_C5_RADIO_STOCK_CAPTURE
/* Keeps upstream's static stock_capture() referenced (-Wunused-function). */
static inline void reference_stock_capture(void) { (void)stock_capture; }
#endif
