// SPDX-License-Identifier: GPL-3.0-or-later
#include "board/battery.hpp"

#include <algorithm>

#include <esp_adc/adc_cali_scheme.h>
#include <esp_check.h>

#include "board/pins.hpp"

namespace board
{
    namespace
    {
        constexpr adc_channel_t channel = ADC_CHANNEL_3; // GPIO4
        constexpr int samples = 8;
        // Above a full cell: USB (with or without a cell) feeds the divider.
        constexpr int external_mv = 4350;
        // Below this there is no sensible reading.
        constexpr int missing_mv = 2500;

        // Single-cell LiPo resting voltage to charge, coarse.
        uint8_t percent_of(int mv)
        {
            constexpr int empty_mv = 3300, full_mv = 4150;

            return static_cast<uint8_t>(std::clamp((mv - empty_mv) * 100 / (full_mv - empty_mv), 0, 100));
        }
    }

    esp_err_t battery::init()
    {
        adc_oneshot_unit_init_cfg_t unit = {};

        unit.unit_id = ADC_UNIT_1;
        ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &m_adc), "battery", "adc unit");

        adc_oneshot_chan_cfg_t config = {};

        config.atten = ADC_ATTEN_DB_12;
        config.bitwidth = ADC_BITWIDTH_DEFAULT;
        ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(m_adc, channel, &config), "battery", "adc channel");

        adc_cali_curve_fitting_config_t cali = {};

        cali.unit_id = ADC_UNIT_1;
        cali.chan = channel;
        cali.atten = ADC_ATTEN_DB_12;
        cali.bitwidth = ADC_BITWIDTH_DEFAULT;

        return adc_cali_create_scheme_curve_fitting(&cali, &m_cali);
    }

    battery_status battery::read()
    {
        battery_status status;
        int sum = 0;

        if (!m_adc || !m_cali)
            return status;

        for (int i = 0; i < samples; i++)
        {
            int raw = 0, mv = 0;

            if (adc_oneshot_read(m_adc, channel, &raw) != ESP_OK || adc_cali_raw_to_voltage(m_cali, raw, &mv) != ESP_OK)
                return status;

            sum += mv;
        }

        const int mv = sum / samples * 2;

        status.millivolts = static_cast<uint16_t>(mv);

        if (mv < missing_mv)
            return status;

        status.percent = percent_of(mv);
        status.source = mv > external_mv ? power_source::external : power_source::discharging;

        return status;
    }
}
