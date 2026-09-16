#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*motor_position_callback_t)(uint8_t lift_percentage, bool moving);

esp_err_t motor_driver_init(motor_position_callback_t callback);
bool motor_driver_set_lift_percentage(uint8_t lift_percentage);
void motor_driver_open(void);
void motor_driver_close(void);
void motor_driver_stop(void);
void motor_driver_mark_closed(void);
bool motor_driver_is_calibrated(void);
uint8_t motor_driver_get_lift_percentage(void);
bool motor_driver_set_speed(uint16_t steps_per_second);
uint16_t motor_driver_get_speed(void);
