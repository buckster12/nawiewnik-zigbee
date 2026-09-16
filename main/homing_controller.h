#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HOMING_STEP,
    HOMING_DEBOUNCE,
    HOMING_SUCCESS,
    HOMING_FAILED,
} homing_action_t;

typedef struct {
    uint32_t max_steps;
    uint32_t steps_taken;
    uint8_t debounce_samples;
    uint8_t active_samples;
    bool finished;
} homing_controller_t;

void homing_controller_init(homing_controller_t *homing, uint32_t max_steps, uint8_t debounce_samples);
homing_action_t homing_controller_update(homing_controller_t *homing, bool sensor_active);
