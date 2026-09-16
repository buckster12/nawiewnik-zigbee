#include "motor_model.h"

#include <stddef.h>

static int32_t clamp_steps(int32_t steps, int32_t travel_steps)
{
    if (steps < 0) {
        return 0;
    }
    if (steps > travel_steps) {
        return travel_steps;
    }
    return steps;
}

void motor_model_init(motor_model_t *motor, int32_t travel_steps)
{
    if (motor == NULL) {
        return;
    }
    motor->current_steps = 0;
    motor->target_steps = 0;
    motor->travel_steps = travel_steps > 0 ? travel_steps : 1;
    motor->phase = 0;
    motor->calibrated = false;
}

void motor_model_restore(motor_model_t *motor, int32_t current_steps, bool calibrated)
{
    if (motor == NULL) {
        return;
    }
    motor->current_steps = clamp_steps(current_steps, motor->travel_steps);
    motor->target_steps = motor->current_steps;
    motor->calibrated = calibrated;
}

void motor_model_mark_closed(motor_model_t *motor)
{
    if (motor == NULL) {
        return;
    }
    motor->current_steps = 0;
    motor->target_steps = 0;
    motor->phase = 0;
    motor->calibrated = true;
}

void motor_model_mark_open(motor_model_t *motor)
{
    if (motor == NULL) {
        return;
    }
    motor->current_steps = motor->travel_steps;
    motor->target_steps = motor->travel_steps;
    motor->calibrated = true;
}

void motor_model_invalidate_position(motor_model_t *motor)
{
    if (motor == NULL) {
        return;
    }
    motor->target_steps = motor->current_steps;
    motor->calibrated = false;
}

bool motor_model_next_open_homing_step(motor_model_t *motor, uint8_t *phase)
{
    if (motor == NULL || phase == NULL) {
        return false;
    }
    motor->phase = (uint8_t)((motor->phase + 1) & 0x07);
    *phase = motor->phase;
    return true;
}

bool motor_model_is_calibrated(const motor_model_t *motor)
{
    return motor != NULL && motor->calibrated;
}

bool motor_model_set_lift_percentage(motor_model_t *motor, uint8_t lift_percentage)
{
    if (motor == NULL || !motor->calibrated || lift_percentage > 100) {
        return false;
    }
    motor->target_steps = (int32_t)(((int64_t)(100 - lift_percentage) * motor->travel_steps + 50) / 100);
    return true;
}

uint8_t motor_model_current_lift_percentage(const motor_model_t *motor)
{
    if (motor == NULL || !motor->calibrated) {
        return MOTOR_POSITION_UNKNOWN;
    }
    int32_t open_percentage = (int32_t)(((int64_t)motor->current_steps * 100 + motor->travel_steps / 2) /
                                       motor->travel_steps);
    return (uint8_t)(100 - open_percentage);
}

bool motor_model_next_step(motor_model_t *motor, uint8_t *phase)
{
    if (motor == NULL || phase == NULL || !motor->calibrated || motor->current_steps == motor->target_steps) {
        return false;
    }
    if (motor->target_steps > motor->current_steps) {
        motor->current_steps++;
        motor->phase = (uint8_t)((motor->phase + 1) & 0x07);
    } else {
        motor->current_steps--;
        motor->phase = (uint8_t)((motor->phase + 7) & 0x07);
    }
    *phase = motor->phase;
    return true;
}

uint8_t motor_model_phase_mask(uint8_t phase)
{
    static const uint8_t phase_masks[8] = {0x01, 0x03, 0x02, 0x06, 0x04, 0x0C, 0x08, 0x09};
    return phase_masks[phase & 0x07];
}

void motor_model_stop(motor_model_t *motor)
{
    if (motor != NULL) {
        motor->target_steps = motor->current_steps;
    }
}
