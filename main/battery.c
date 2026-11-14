#include "battery.h"
#include "sdkconfig.h"

#ifdef CONFIG_MVP_BATTERY_ADC
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"

unsigned battery_blinks(void)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&unit, &adc) != ESP_OK) {
        return 0;
    }
    adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_curve_fitting_config_t calibration = {
        .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_handle_t cal;
    int sum = 0, samples = 0;
    if (adc_oneshot_config_channel(adc, ADC_CHANNEL_0, &channel) == ESP_OK &&
        adc_cali_create_scheme_curve_fitting(&calibration, &cal) == ESP_OK) {
        for (int i = 0; i < 16; ++i) {
            int raw, mv;
            if (adc_oneshot_read(adc, ADC_CHANNEL_0, &raw) == ESP_OK &&
                adc_cali_raw_to_voltage(cal, raw, &mv) == ESP_OK) {
                sum += mv;
                ++samples;
            }
        }
        adc_cali_delete_scheme_curve_fitting(cal);
    }
    adc_oneshot_del_unit(adc);
    int mv = samples ? 3 * sum / samples : 0;
    if (mv < 2800 || mv > 4350) {
        return 0;
    }
    // Approximate state of charge for a single Li-ion cell by voltage.
    return 1 + (mv >= 3700) + (mv >= 3850) + (mv >= 4100);
}
#else
unsigned battery_blinks(void)
{
    return 0;
}
#endif
