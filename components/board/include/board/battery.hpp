// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include <driver/i2c_master.h>
#include <esp_err.h>

namespace board
{
    struct battery_status
    {
        bool present = false; // gauge answered and a cell is connected
        bool charging = false;
        uint8_t percent = 0;
        uint16_t millivolts = 0;
    };

    // AXP2602 fuel gauge on I2C (address 0x62).
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
}
