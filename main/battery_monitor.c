#include "battery_monitor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BATTERY_ADC_UNIT ADC_UNIT_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3 /* GPIO4 on ESP32-H2 */
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_2_5
#define BATTERY_SAMPLE_COUNT 64
#define BATTERY_DIVIDER_TOP_OHM 300000U
#define BATTERY_DIVIDER_BOTTOM_OHM 95000U

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_calibrated;

esp_err_t battery_monitor_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "ADC unit init failed");

    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &channel_cfg), TAG,
                        "ADC channel config failed");

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&curve_cfg, &s_cali) == ESP_OK) {
        s_calibrated = true;
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!s_calibrated) {
        adc_cali_line_fitting_config_t line_cfg = {
            .unit_id = BATTERY_ADC_UNIT,
            .atten = BATTERY_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_line_fitting(&line_cfg, &s_cali) == ESP_OK) {
            s_calibrated = true;
        }
    }
#endif
    ESP_RETURN_ON_FALSE(s_calibrated, ESP_ERR_NOT_SUPPORTED, TAG, "ADC calibration unavailable");
    ESP_LOGI(TAG, "Initialized GPIO4 ADC: divider=%u/%u ohm", BATTERY_DIVIDER_TOP_OHM,
             BATTERY_DIVIDER_BOTTOM_OHM);
    return ESP_OK;
}

esp_err_t battery_monitor_read_mv(uint16_t *battery_mv)
{
    ESP_RETURN_ON_FALSE(battery_mv != NULL, ESP_ERR_INVALID_ARG, TAG, "battery_mv is NULL");

    int64_t raw_sum = 0;
    for (size_t i = 0; i < BATTERY_SAMPLE_COUNT; ++i) {
        int raw;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw), TAG, "ADC read failed");
        raw_sum += raw;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    int adc_mv;
    int raw_average = (int)((raw_sum + BATTERY_SAMPLE_COUNT / 2) / BATTERY_SAMPLE_COUNT);
    ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw_average, &adc_mv), TAG,
                        "ADC calibration conversion failed");

    uint32_t cell_mv = ((uint32_t)adc_mv *
                        (BATTERY_DIVIDER_TOP_OHM + BATTERY_DIVIDER_BOTTOM_OHM) +
                        BATTERY_DIVIDER_BOTTOM_OHM / 2) /
                       BATTERY_DIVIDER_BOTTOM_OHM;
    ESP_RETURN_ON_FALSE(cell_mv <= UINT16_MAX, ESP_ERR_INVALID_SIZE, TAG, "Battery voltage overflow");
    *battery_mv = (uint16_t)cell_mv;
    ESP_LOGI(TAG, "ADC raw=%d pin=%d mV battery=%u mV", raw_average, adc_mv, *battery_mv);
    return ESP_OK;
}

uint8_t battery_monitor_percentage(uint16_t mv)
{
    /* Resting-voltage approximation for a single Li-ion cell. */
    static const struct {
        uint16_t mv;
        uint8_t percent;
    } curve[] = {
        {3300, 0}, {3500, 3}, {3600, 7}, {3700, 15}, {3750, 25}, {3800, 40},
        {3850, 55}, {3900, 65}, {4000, 80}, {4100, 92}, {4200, 100},
    };

    if (mv <= curve[0].mv) {
        return 0;
    }
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
        if (mv <= curve[i].mv) {
            uint32_t span_mv = curve[i].mv - curve[i - 1].mv;
            uint32_t span_pct = curve[i].percent - curve[i - 1].percent;
            return (uint8_t)(curve[i - 1].percent +
                             ((uint32_t)(mv - curve[i - 1].mv) * span_pct + span_mv / 2) / span_mv);
        }
    }
    return 100;
}
