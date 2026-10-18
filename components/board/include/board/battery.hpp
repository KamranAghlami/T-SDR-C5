// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include <esp_err.h>
#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32S3
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>
#else
#include <driver/i2c_master.h>
#endif

namespace board
{
    enum class power_source : uint8_t
    {
        unknown,     // no gauge or no reading
        external,    // no cell current: on USB with the cell full, or no cell at all
        charging,
        discharging, // running from the cell
    };

    struct battery_status
    {
        power_source source = power_source::unknown;
        uint8_t percent = 0;
        uint16_t millivolts = 0;
        int16_t milliamps = 0; // positive while charging; 0 without a gauge
    };

#if CONFIG_IDF_TARGET_ESP32S3
    // T-Display-S3: battery voltage on an ADC pin through a 1:2 divider. There
    // is no fuel gauge or charger status, so the voltage alone decides: USB
    // lifts the pin above any cell voltage.
    class battery
    {
    public:
        esp_err_t init();

        battery_status read();

    private:
        adc_oneshot_unit_handle_t m_adc = nullptr;
        adc_cali_handle_t m_cali = nullptr;
    };
#else
    // AXP2602 fuel gauge on I2C (address 0x62). It has no presence or charger
    // status, and the TP4065 charger's CHRG pin only drives an LED, so the
    // cell current decides: with USB connected and no cell, the charger holds
    // the battery pin near 4.2 V, which is why the voltage can't tell.
    class battery
    {
    public:
        esp_err_t init();

        battery_status read();

    private:
        esp_err_t read_registers(uint8_t reg, uint8_t *data, size_t size);

        i2c_master_bus_handle_t m_bus = nullptr;
        i2c_master_dev_handle_t m_device = nullptr;
    };
#endif
}
