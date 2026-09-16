#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MOTOR_POSITION_UNKNOWN UINT8_C(0xFF)

typedef struct {
    int32_t current_steps;
    int32_t target_steps;
    int32_t travel_steps;
    uint8_t phase;
    bool calibrated;
} motor_model_t;

void motor_model_init(motor_model_t *motor, int32_t travel_steps);
void motor_model_restore(motor_model_t *motor, int32_t current_steps, bool calibrated);
void motor_model_mark_closed(motor_model_t *motor);
void motor_model_mark_open(motor_model_t *motor);
void motor_model_invalidate_position(motor_model_t *motor);
bool motor_model_next_open_homing_step(motor_model_t *motor, uint8_t *phase);
bool motor_model_is_calibrated(const motor_model_t *motor);
bool motor_model_set_lift_percentage(motor_model_t *motor, uint8_t lift_percentage);
uint8_t motor_model_current_lift_percentage(const motor_model_t *motor);
bool motor_model_next_step(motor_model_t *motor, uint8_t *phase);
uint8_t motor_model_phase_mask(uint8_t phase);
void motor_model_stop(motor_model_t *motor);
