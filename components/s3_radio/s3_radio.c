/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Receive setup and snapshot capture for the ESP32-S3, adapted from ESP-SDR's
 * S3 receiver (third_party/esp-sdr/main/targets/esp32s3/receiver.c, Copyright
 * ESP-SDR contributors / ESPARGOS). The serial protocol and the continuous
 * ring modes are dropped; the PHY sequence, LO plan, analog filter handling,
 * gain control and capture sentinels follow upstream. */
#include <string.h>
#include "sdkconfig.h"
#include "s3_radio.h"
#include "esp_event.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "heap_memory_layout.h"
#include "nvs_flash.h"
#include "soc/soc.h"

#include "rx_bandwidth.h"
#include "rx_lo.h"
#include "rx_recalibration.h"

/* Vendor S3 adctrig dumps into the 64 KiB aperture at 0x3fcd0000
 * (MAC_DUMP_USAGE=4). Keep it out of the heap; sram_guard.ld keeps static
 * sections (DRAM and the IRAM alias at 0x403c0000) below it. */
#define DUMP_BASE 0x3fcd0000u
#define DUMP_END 0x3fce0000u
SOC_RESERVE_MEMORY_REGION(DUMP_BASE, DUMP_END, s3_rf_dump);
#define DUMP_WORDS ((volatile uint32_t *)DUMP_BASE)

#define SRAM_OWNER_REG 0x600c101cu
#define DUMP_CTRL 0x60033d5cu /* 0-14 count, 15 40 MS/s, 16 16 MS/s, 18 done, 19 trigger, 31 enable */
#define DUMP_PACK 0x60033d90u
#define GAIN_REG 0x6001c02cu
#define SENTINEL 0xa5a0055au
#define CANARY 0x5a5aa5a5u
#define TIMEOUT_US 20000
/* Receiver settling after a full RX setup (upstream), and after a survey step. */
#define SETTLE_US 3000
#define SETTLE_FAST_US 1000
/* A PHY that re-measures DC while we set up the channel would otherwise make
 * every capture recalibrate; between retunes, accept at most this rate. */
#define RECALIBRATE_INTERVAL_US 500000
#define TRY(x) do { esp_err_t err_ = (x); if (err_ != ESP_OK) return err_; } while (0)

extern void stop_tx_tone(unsigned);
extern void rom_pbus_workmode(void);
extern void rom_pbus_xpd_rx_on(unsigned);
extern void rom_pbus_xpd_tx_off(void);
extern void rom_set_rxclk_en(unsigned);
extern void set_chanfreq(unsigned, unsigned);
extern void set_rf_freq_offset(unsigned, unsigned, int);
extern void force_rx_gain(unsigned, unsigned, unsigned);

/* Required by the stock RF test archive; no shell is exposed. */
int cmd_parse(char *cmd, char *name, int *argc, char **argv) {
    (void)cmd; (void)name; (void)argc; (void)argv;
    return -1;
}

static unsigned frequency_mhz = 2412;
static int analog_filter = -1; /* -1: PHY-calibrated capacitor code */
static bool rx_ready;
static bool gain_manual;
static unsigned gain_code = 40;
static s3_radio_stats_t stats;

static bool frequency_valid(unsigned mhz) { return mhz >= 100 && mhz <= 6000; }

unsigned s3_radio_gain_max(void) {
    unsigned maximum = (REG_READ(GAIN_REG) >> 8) & 127u;
    return maximum <= 82u ? maximum : 0u; /* never expose uncalibrated slots */
}

static void gain_apply(void) {
    if (gain_code > s3_radio_gain_max()) gain_code = s3_radio_gain_max();
    force_rx_gain(gain_manual, gain_code, 0);
}

/* RX-only analog capacitance, applied per capture and restored afterwards so
 * PHY calibration stays intact across retunes. */
static void analog_apply(unsigned saved[2]) {
    for (unsigned j = 0; j < 2; j++) {
        saved[j] = rom_chip_i2c_readReg(0x67, 0, 4 + j);
        if (analog_filter >= 0)
            rom_chip_i2c_writeReg(0x67, 0, 4 + j, (saved[j] & ~63u) | (unsigned)analog_filter);
    }
}

static void analog_restore(const unsigned saved[2]) {
    if (analog_filter >= 0)
        for (unsigned j = 0; j < 2; j++) rom_chip_i2c_writeReg(0x67, 0, 4 + j, saved[j]);
}

/* Wi-Fi channel centres tune directly; everything else goes through the RF
 * frequency offset from channel 1. 1842..2209 MHz uses the 6/5 LO divider. */
static void set_channel(unsigned mhz) {
    const rx_lo_plan_t plan = rx_lo_plan(mhz);
    const bool channel = (mhz >= 2412 && mhz <= 2472 && (mhz - 2412) % 5 == 0) || mhz == 2484;
    rx_lo_select(false);
    set_chanfreq(channel ? mhz : 2412, 0);
    if (!channel) set_rf_freq_offset(0, plan.mhz, plan.offset_khz);
}

static void rx_on(unsigned settle_us) {
    stop_tx_tone(1);
    rom_pbus_workmode();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(1);
    rom_set_rxclk_en(1);
    gain_apply();
    rx_lo_select(rx_lo_plan(frequency_mhz).alternate);
    esp_rom_delay_us(settle_us);
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
    set_channel(frequency_mhz);
    rx_on(SETTLE_US);
    rx_ready = true;
}

esp_err_t s3_radio_init(void) {
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

esp_err_t s3_radio_tune(unsigned mhz) {
    if (!frequency_valid(mhz)) return ESP_ERR_INVALID_ARG;
    frequency_mhz = mhz;
    rx_ready = false;
    prepare_rx();
    return ESP_OK;
}

/* Survey steps: program the channel but keep the current DC calibration.
 * The display removes DC per capture anyway; the PHY's own staleness signal
 * still triggers a (rate-limited) recalibration in prepare_rx(). */
esp_err_t s3_radio_tune_fast(unsigned mhz) {
    if (!frequency_valid(mhz)) return ESP_ERR_INVALID_ARG;
    frequency_mhz = mhz;
    set_channel(mhz);
    rx_on(SETTLE_FAST_US);
    rx_ready = true;
    return ESP_OK;
}

unsigned s3_radio_frequency(void) { return frequency_mhz; }

void s3_radio_gain_hardware(void) {
    gain_manual = false;
    gain_apply();
}

void s3_radio_gain_manual(unsigned index) {
    gain_manual = true;
    gain_code = index;
    gain_apply();
}

esp_err_t s3_radio_bandwidth(unsigned mhz) {
    if (mhz && (mhz < RX_BANDWIDTH_MIN || mhz > RX_BANDWIDTH_MAX)) return ESP_ERR_INVALID_ARG;
    analog_filter = mhz ? (int)rx_bandwidth_dcap(mhz) : -1;
    return ESP_OK;
}

/* Finite dump of n samples into the aperture, as upstream capture(). */
static esp_err_t engine_capture(unsigned n, unsigned rate, const uint32_t **words, uint32_t *elapsed_us) {
    volatile uint32_t *w = DUMP_WORDS;
    for (unsigned j = 0; j < n; j++) w[j] = SENTINEL;
    for (unsigned j = 0; j < 4; j++) w[n + j] = CANARY ^ j;
    const uint32_t owner = REG_READ(SRAM_OWNER_REG);
    const int64_t start = esp_timer_get_time();
    REG_WRITE(DUMP_CTRL, 0);
    REG_WRITE(DUMP_PACK, 0u | (1u << 6) | (2u << 12) | (3u << 18));
    REG_WRITE(SRAM_OWNER_REG, (owner & ~15u) | 4u);
    const uint32_t rate_bits = rate == S3_RATE_40M ? (1u << 15) : rate == S3_RATE_16M ? (1u << 16) : 0u;
    const uint32_t ctrl = 0x80000000u | rate_bits | n;
    REG_WRITE(DUMP_CTRL, ctrl);
    REG_WRITE(DUMP_CTRL, ctrl | (1u << 19));
    REG_WRITE(DUMP_CTRL, ctrl);
    while (!(REG_READ(DUMP_CTRL) & (1u << 18)) && esp_timer_get_time() - start < TIMEOUT_US) {
    }
    const bool done = (REG_READ(DUMP_CTRL) & (1u << 18)) != 0;
    if (elapsed_us) *elapsed_us = (uint32_t)(esp_timer_get_time() - start);
    /* CPU reads of the aperture are garbage until ownership is returned. The
     * read-back makes sure the switch has landed before the sentinel check
     * (without it most snapshots failed it on hardware). */
    REG_WRITE(DUMP_CTRL, 0);
    REG_WRITE(SRAM_OWNER_REG, owner);
    __asm__ volatile("memw" ::: "memory");
    (void)REG_READ(SRAM_OWNER_REG);
    if (!done) return ESP_ERR_TIMEOUT;
    for (unsigned j = 0; j < n; j++)
        if (w[j] == SENTINEL) return ESP_ERR_TIMEOUT;
    for (unsigned j = 0; j < 4; j++)
        if (w[n + j] != (CANARY ^ j)) return ESP_ERR_INVALID_SIZE;
    *words = (const uint32_t *)DUMP_WORDS;
    return ESP_OK;
}

esp_err_t s3_radio_capture(unsigned n, s3_rate_t rate, const uint32_t **words, uint32_t *elapsed_us) {
    if (n < 256 || n > S3_RADIO_MAX_SAMPLES) return ESP_ERR_INVALID_ARG;
    if (rate != S3_RATE_80M && rate != S3_RATE_40M && rate != S3_RATE_16M) return ESP_ERR_INVALID_ARG;
    prepare_rx();
    unsigned saved[2];
    analog_apply(saved);
    esp_err_t e = engine_capture(n, (unsigned)rate, words, elapsed_us);
    analog_restore(saved);
    if (e == ESP_OK) stats.captures++;
    else stats.failures++;
    return e;
}

void s3_radio_get_stats(s3_radio_stats_t *out) { *out = stats; }
