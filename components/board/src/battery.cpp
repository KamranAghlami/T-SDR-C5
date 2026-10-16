// SPDX-License-Identifier: GPL-3.0-or-later
#include "board/battery.hpp"

#include <esp_check.h>

#include "board/pins.hpp"

// Register map from LilyGO's AXP2602 driver (T-Display-C5 repository).
namespace board
{
    namespace
    {
        constexpr uint16_t address = 0x62;
        constexpr uint8_t reg_id = 0x00, reg_mode = 0x02, reg_vbat = 0x04, reg_soc = 0x08, reg_ibat = 0x14;
        constexpr uint8_t chip_id = 0x1c;
        constexpr int timeout_ms = 20;
        // Without a cell the gauge reads close to 0 V.
        constexpr uint16_t min_cell_mv = 2500;
    }

    esp_err_t battery::init()
    {
        i2c_master_bus_config_t bus = {};

        bus.i2c_port = I2C_NUM_0;
        bus.sda_io_num = pins::i2c_sda;
        bus.scl_io_num = pins::i2c_scl;
        bus.clk_source = I2C_CLK_SRC_DEFAULT;
        bus.glitch_ignore_cnt = 7;
        bus.flags.enable_internal_pullup = true;

        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &m_bus), "battery", "i2c bus");

        i2c_device_config_t device = {};

        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = address;
        device.scl_speed_hz = 100'000;

        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(m_bus, &device, &m_device), "battery", "i2c device");

        uint8_t id = 0, mode = 0;

        ESP_RETURN_ON_ERROR(read_registers(reg_id, &id, 1), "battery", "no gauge");

        if (id != chip_id)
            return ESP_ERR_NOT_FOUND;

        // Wake the gauge (clear the sleep bit).
        ESP_RETURN_ON_ERROR(read_registers(reg_mode, &mode, 1), "battery", "mode");

        if (mode & 1)
        {
            const uint8_t write[2] = {reg_mode, static_cast<uint8_t>(mode & ~1u)};

            ESP_RETURN_ON_ERROR(i2c_master_transmit(m_device, write, sizeof(write), timeout_ms), "battery", "wake");
        }

        return ESP_OK;
    }

    esp_err_t battery::read_registers(uint8_t reg, uint8_t *data, size_t size)
    {
        return i2c_master_transmit_receive(m_device, &reg, 1, data, size, timeout_ms);
    }

    battery_status battery::read()
    {
        battery_status status;
        uint8_t vbat[2], soc = 0, ibat[2];

        if (!m_device || read_registers(reg_vbat, vbat, 2) != ESP_OK || read_registers(reg_soc, &soc, 1) != ESP_OK ||
            read_registers(reg_ibat, ibat, 2) != ESP_OK)
            return status;

        status.millivolts = static_cast<uint16_t>(((vbat[0] << 8) | vbat[1]) & 0x3fff);
        status.present = status.millivolts >= min_cell_mv;
        status.percent = soc > 100 ? 100 : soc;
        // Signed, big-endian; positive while charging.
        status.charging = static_cast<int16_t>((ibat[0] << 8) | ibat[1]) > 0;

        return status;
    }
}
