/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Register-level snapshot engine for the ESP32-C5 RF dump, ported from the
 * ESPsoup firmware (main/families/c5_c6_c61/c5_engine.h, Copyright (C) 2026
 * ESPsoup contributors / Peter Holzhauser, GPL-3.0-or-later).
 *
 * The vendor adctrig() does a read-modify-write of the "source-15 gate"
 * register 0x600a20b4 on every call, and each access stalls the bus for about
 * 0.82 ms: about 1.7 ms of overhead per snapshot. That bit is clear after boot,
 * so this engine checks it once and otherwise programs the dump block
 * directly. Measured by ESPsoup at 80 MS/s: 1886 -> 206 us per 16380 samples.
 *
 * Only the single-half path (n <= 16380) is ported. */
#include "c5_engine.h"

#include "esp_attr.h"
#include "esp_timer.h"
#include "soc/soc.h"

#define SRAM_OWNER_REG 0x60095004u
#define DUMP_CTRL 0x600a9004u   /* 0-16 count, 18 done, 19 software trigger, 31 enable */
#define DUMP_MODE 0x600a9008u   /* 17-20 source, 21-23 divider, 0-16 live position */
#define DUMP_PACK 0x600a9018u
#define DUMP_WORDS ((volatile uint32_t *)0x40820000u) /* lower half of SRAM bank 1 */
#define GATE_REG 0x600a20b4u
#define SENTINEL 0xa5a0055au
#define CANARY 0x5a5aa5a5u
#define TIMEOUT_US 20000

static void dump_arm(unsigned divider, uint32_t count, uint32_t *owner_saved) {
    *owner_saved = REG_READ(SRAM_OWNER_REG);
    REG_WRITE(DUMP_CTRL, 0);
    REG_WRITE(DUMP_MODE, (REG_READ(DUMP_MODE) & ~0x00e00000u) | ((divider & 7u) << 21));
    REG_WRITE(0x600a9c04, 0xffffffffu); /* dump clocks, as adctrig */
    REG_SET_BIT(0x600a0800, 4);
    REG_WRITE(DUMP_CTRL, count & 0x1ffffu);
    REG_WRITE(DUMP_PACK, (REG_READ(DUMP_PACK) & ~0x00ffffffu) | 24u | (25u << 6) | (26u << 12) | (27u << 18));
    REG_SET_BIT(DUMP_PACK, 1u << 24);
    /* Hand bank 1 (owner field = 2), lower 64 KiB half, to the modem. */
    REG_WRITE(SRAM_OWNER_REG, (*owner_saved & ~0x10f00u) | 0x200u);
    __asm__ volatile("fence rw,rw" ::: "memory");
    (void)REG_READ(SRAM_OWNER_REG);
    static bool gate_checked;
    if (!gate_checked) {
        if (REG_READ(GATE_REG) & 1u) REG_CLR_BIT(GATE_REG, 1u);
        gate_checked = true;
    }
    REG_WRITE(DUMP_MODE, (REG_READ(DUMP_MODE) & ~0x001e0000u) | (15u << 17));
}

static void dump_go(void) {
    REG_SET_BIT(DUMP_CTRL, 1u << 31);
    REG_SET_BIT(DUMP_CTRL, 1u << 19);
    REG_CLR_BIT(DUMP_CTRL, 1u << 19);
}

static void dump_halt(uint32_t owner_saved) {
    REG_CLR_BIT(DUMP_CTRL, 1u << 31);
    REG_WRITE(DUMP_CTRL, 0);
    REG_WRITE(SRAM_OWNER_REG, owner_saved);
    __asm__ volatile("fence rw,rw" ::: "memory");
    (void)REG_READ(SRAM_OWNER_REG);
}

esp_err_t c5_engine_capture(unsigned n, unsigned divider, const uint32_t **words, uint32_t *elapsed_us) {
    volatile uint32_t *w = DUMP_WORDS;
    if (n > C5_ENGINE_MAX_SAMPLES) return ESP_ERR_INVALID_ARG;
    for (unsigned j = 0; j < n; j++) w[j] = SENTINEL;
    for (unsigned j = 0; j < 4; j++) w[n + j] = CANARY ^ j;
    uint32_t owner;
    dump_arm(divider, n, &owner);
    int64_t start = esp_timer_get_time();
    dump_go();
    while (!(REG_READ(DUMP_CTRL) & (1u << 18)) && esp_timer_get_time() - start < TIMEOUT_US) {
    }
    bool done = (REG_READ(DUMP_CTRL) & (1u << 18)) != 0;
    if (elapsed_us) *elapsed_us = (uint32_t)(esp_timer_get_time() - start);
    /* CPU reads of bank 1 are garbage until ownership is returned. */
    dump_halt(owner);
    if (!done) return ESP_ERR_TIMEOUT;
    for (unsigned j = 0; j < n; j++)
        if (w[j] == SENTINEL) return ESP_ERR_TIMEOUT;
    for (unsigned j = 0; j < 4; j++)
        if (w[n + j] != (CANARY ^ j)) return ESP_ERR_INVALID_SIZE;
    *words = (const uint32_t *)DUMP_WORDS;
    return ESP_OK;
}
