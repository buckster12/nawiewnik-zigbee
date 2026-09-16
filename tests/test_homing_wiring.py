from pathlib import Path

source = (Path(__file__).parents[1] / "main" / "motor_driver.c").read_text()

assert "#define HALL_ENDSTOP_GPIO GPIO_NUM_2" in source
assert ".pull_up_en = GPIO_PULLUP_ENABLE" in source
assert "gpio_get_level(HALL_ENDSTOP_GPIO) == 0" in source
assert "homing_controller_update" in source
assert "motor_model_mark_open" in source
assert "motor_model_invalidate_position" in source
assert "HOMING_MAX_STEPS_MULTIPLIER" in source

# Every normal boot must establish an absolute reference by moving toward OPEN
# until the active-low reed switch closes, with the bounded homing controller
# providing the safety stop when the magnet is not detected.
init_body = source.split("esp_err_t motor_driver_init", 1)[1].split("bool motor_driver_set_lift_percentage", 1)[0]
assert "motor_driver_home_open();" in init_body
assert "gpio_get_level(HALL_ENDSTOP_GPIO) == 0" in source
assert "motor_model_mark_open(&s_motor);" in source

print("homing wiring tests: PASS")
