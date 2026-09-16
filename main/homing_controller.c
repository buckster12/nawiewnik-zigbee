#include "homing_controller.h"

#include <stddef.h>

void homing_controller_init(homing_controller_t *homing, uint32_t max_steps, uint8_t debounce_samples)
{
    if (homing == NULL) {
        return;
    }
    homing->max_steps = max_steps;
    homing->steps_taken = 0;
    homing->debounce_samples = debounce_samples > 0 ? debounce_samples : 1;
    homing->active_samples = 0;
    homing->finished = false;
}

homing_action_t homing_controller_update(homing_controller_t *homing, bool sensor_active)
{
    if (homing == NULL || homing->finished) {
        return HOMING_FAILED;
    }
    if (sensor_active) {
        homing->active_samples++;
        if (homing->active_samples >= homing->debounce_samples) {
            homing->finished = true;
            return HOMING_SUCCESS;
        }
        return HOMING_DEBOUNCE;
    }
    homing->active_samples = 0;
    if (homing->steps_taken >= homing->max_steps) {
        homing->finished = true;
        return HOMING_FAILED;
    }
    homing->steps_taken++;
    return HOMING_STEP;
}
