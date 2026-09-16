#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "homing_controller.h"

static void test_normally_open_reed_requires_stable_low_samples(void)
{
    homing_controller_t homing;
    homing_controller_init(&homing, 100, 3);

    assert(homing_controller_update(&homing, true) == HOMING_DEBOUNCE);
    assert(homing_controller_update(&homing, false) == HOMING_STEP);
    assert(homing_controller_update(&homing, true) == HOMING_DEBOUNCE);
    assert(homing_controller_update(&homing, true) == HOMING_DEBOUNCE);
    assert(homing_controller_update(&homing, true) == HOMING_SUCCESS);
}

static void test_missing_reed_fails_after_bounded_step_budget(void)
{
    homing_controller_t homing;
    homing_controller_init(&homing, 2, 3);

    assert(homing_controller_update(&homing, false) == HOMING_STEP);
    assert(homing_controller_update(&homing, false) == HOMING_STEP);
    assert(homing_controller_update(&homing, false) == HOMING_FAILED);
}

int main(void)
{
    test_normally_open_reed_requires_stable_low_samples();
    test_missing_reed_fails_after_bounded_step_budget();
    puts("homing_controller tests: PASS");
    return 0;
}
