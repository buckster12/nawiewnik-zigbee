#!/usr/bin/env python3
from pathlib import Path

src = (Path(__file__).parents[1] / "main" / "motor_driver.c").read_text()

required = [
    "#define MOTOR_POWER_ENABLE_GPIO GPIO_NUM_3",
    "static void motor_power_enable(void)",
    "static void motor_power_disable(void)",
    "gpio_set_level(MOTOR_POWER_ENABLE_GPIO, 1)",
    "gpio_set_level(MOTOR_POWER_ENABLE_GPIO, 0)",
]
for needle in required:
    assert needle in src, f"missing motor rail control: {needle}"

# The first phase must only be applied after the converter has been enabled
# and given time to establish 5 V.
step_block_start = src.index("if (stepped) {")
step_block_end = src.index("continue;", step_block_start)
step_block = src[step_block_start:step_block_end]
assert step_block.index("motor_power_enable();") < step_block.index("motor_apply_mask(")
assert "vTaskDelay(pdMS_TO_TICKS(MOTOR_POWER_STARTUP_MS))" in step_block

# Shutdown order: remove phase drive first, then disable the 5 V rail.
idle_start = src.index("motor_deenergize();", step_block_end)
idle_end = src.index("if (was_moving)", idle_start)
idle_block = src[idle_start:idle_end]
assert idle_block.index("motor_deenergize();") < idle_block.index("motor_power_disable();")

# Firmware must boot with the rail explicitly disabled.
init_start = src.index("esp_err_t motor_driver_init")
init_end = src.index("motor_model_init", init_start)
init_block = src[init_start:init_end]
assert "MOTOR_POWER_ENABLE_GPIO" in init_block
assert "motor_power_disable();" in init_block

print("motor power sequencing tests passed")
