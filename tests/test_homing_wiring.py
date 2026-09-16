from pathlib import Path

source = (Path(__file__).parents[1] / "main" / "motor_driver.c").read_text()

assert "#define HALL_ENDSTOP_GPIO GPIO_NUM_2" in source
assert ".pull_up_en = GPIO_PULLUP_ENABLE" in source
assert "gpio_get_level(HALL_ENDSTOP_GPIO) == 0" in source
assert "homing_controller_update" in source
assert "motor_model_mark_open" in source
assert "motor_model_invalidate_position" in source
assert "HOMING_MAX_STEPS_MULTIPLIER" in source

print("homing wiring tests: PASS")
