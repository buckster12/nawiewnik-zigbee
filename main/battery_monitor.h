#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t battery_monitor_init(void);
esp_err_t battery_monitor_read_mv(uint16_t *battery_mv);
uint8_t battery_monitor_percentage(uint16_t battery_mv);
