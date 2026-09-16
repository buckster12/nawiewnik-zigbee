#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "motor_model.h"

static void test_uncalibrated_model_rejects_motion(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);

    assert(!motor_model_is_calibrated(&motor));
    assert(!motor_model_set_lift_percentage(&motor, 0));
    assert(motor_model_current_lift_percentage(&motor) == MOTOR_POSITION_UNKNOWN);
}

static void test_mark_closed_establishes_known_zero(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);

    motor_model_mark_closed(&motor);

    assert(motor_model_is_calibrated(&motor));
    assert(motor.current_steps == 0);
    assert(motor.target_steps == 0);
    assert(motor_model_current_lift_percentage(&motor) == 100);
}

static void test_open_homing_can_step_before_calibration(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);

    uint8_t phase = 0;
    assert(motor_model_next_open_homing_step(&motor, &phase));
    assert(phase == 1);
    assert(motor.current_steps == 0);
    assert(!motor_model_is_calibrated(&motor));
}

static void test_mark_open_establishes_open_endpoint(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);

    motor_model_mark_open(&motor);

    assert(motor_model_is_calibrated(&motor));
    assert(motor.current_steps == 1024);
    assert(motor.target_steps == 1024);
    assert(motor_model_current_lift_percentage(&motor) == 0);
}

static void test_invalidate_position_blocks_normal_motion_after_failed_homing(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);
    motor_model_mark_closed(&motor);

    motor_model_invalidate_position(&motor);

    assert(!motor_model_is_calibrated(&motor));
    assert(!motor_model_set_lift_percentage(&motor, 50));
    assert(motor_model_current_lift_percentage(&motor) == MOTOR_POSITION_UNKNOWN);
}

static void test_lift_percentage_maps_open_to_full_travel(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);
    motor_model_mark_closed(&motor);

    assert(motor_model_set_lift_percentage(&motor, 0));
    assert(motor.target_steps == 1024);

    assert(motor_model_set_lift_percentage(&motor, 50));
    assert(motor.target_steps == 512);

    assert(motor_model_set_lift_percentage(&motor, 100));
    assert(motor.target_steps == 0);
}

static void test_step_advances_one_half_step_and_stops_at_target(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);
    motor_model_mark_closed(&motor);
    assert(motor_model_set_lift_percentage(&motor, 0));

    uint8_t phase = 0;
    assert(motor_model_next_step(&motor, &phase));
    assert(motor.current_steps == 1);
    assert(phase == 1);

    motor.current_steps = 1023;
    motor.phase = 7;
    assert(motor_model_next_step(&motor, &phase));
    assert(motor.current_steps == 1024);
    assert(phase == 0);
    assert(!motor_model_next_step(&motor, &phase));
}

static void test_reverse_step_wraps_phase_and_stop_holds_current_position(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);
    motor_model_restore(&motor, 1000, true);
    assert(motor_model_set_lift_percentage(&motor, 100));

    uint8_t phase = 0;
    assert(motor_model_next_step(&motor, &phase));
    assert(motor.current_steps == 999);
    assert(phase == 7);

    motor_model_stop(&motor);
    assert(motor.target_steps == motor.current_steps);
    assert(!motor_model_next_step(&motor, &phase));
}

static void test_restore_clamps_corrupt_persisted_position(void)
{
    motor_model_t motor;
    motor_model_init(&motor, 1024);

    motor_model_restore(&motor, 999999, true);

    assert(motor.current_steps == 1024);
    assert(motor.target_steps == 1024);
    assert(motor_model_current_lift_percentage(&motor) == 0);
}

static void test_half_step_phase_masks_match_uln2003_sequence(void)
{
    static const uint8_t expected[8] = {
        0x01, /* A */
        0x03, /* A+B */
        0x02, /* B */
        0x06, /* B+C */
        0x04, /* C */
        0x0C, /* C+D */
        0x08, /* D */
        0x09, /* D+A */
    };

    for (uint8_t phase = 0; phase < 8; ++phase) {
        assert(motor_model_phase_mask(phase) == expected[phase]);
    }
    assert(motor_model_phase_mask(8) == expected[0]);
}

int main(void)
{
    test_uncalibrated_model_rejects_motion();
    test_mark_closed_establishes_known_zero();
    test_open_homing_can_step_before_calibration();
    test_mark_open_establishes_open_endpoint();
    test_invalidate_position_blocks_normal_motion_after_failed_homing();
    test_lift_percentage_maps_open_to_full_travel();
    test_step_advances_one_half_step_and_stops_at_target();
    test_reverse_step_wraps_phase_and_stop_holds_current_position();
    test_restore_clamps_corrupt_persisted_position();
    test_half_step_phase_masks_match_uln2003_sequence();
    puts("motor_model tests: PASS");
    return 0;
}
