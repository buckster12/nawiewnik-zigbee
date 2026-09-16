from pathlib import Path

source = (Path(__file__).parents[1] / "main" / "motor_driver.c").read_text()

assert "#define HALL_ENDSTOP_GPIO GPIO_NUM_2" in source
assert ".pull_up_en = GPIO_PULLUP_ENABLE" in source
assert "gpio_get_level(HALL_ENDSTOP_GPIO) == 0" in source
assert "homing_controller_update" in source
assert "motor_model_mark_open" in source
assert "motor_model_invalidate_position" in source
assert "HOMING_MAX_STEPS_MULTIPLIER" in source

# A normal reboot must never move the actuator. Homing remains available only
# as an explicit operation, not from motor_driver_init().
init_body = source.split("esp_err_t motor_driver_init", 1)[1].split("bool motor_driver_set_lift_percentage", 1)[0]
assert "motor_home_open();" not in init_body
assert "motor_driver_home_open();" not in init_body

# One-time migration for devices invalidated by the old unconditional boot
# homing: that failure preserved position==travel at the physical OPEN stop.
assert 'BOOT_HOMING_RECOVERY_KEY "home_fix"' in source
assert "position == travel" in source
assert "motor_model_mark_open(&s_motor);" in init_body
assert "Recovered OPEN calibration after legacy boot-homing failure" in source

print("homing wiring tests: PASS")
