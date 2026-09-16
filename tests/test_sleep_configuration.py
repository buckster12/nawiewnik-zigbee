#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
def text(path):
    return (root / path).read_text()

sdk = text("sdkconfig.defaults")
main = text("main/main.c")
motor = text("main/motor_driver.c")
led = text("main/status_led.c")

required_sdk = [
    "CONFIG_ZB_ZED=y",
    "CONFIG_PM_ENABLE=y",
    "CONFIG_FREERTOS_USE_TICKLESS_IDLE=y",
    "CONFIG_IEEE802154_SLEEP_ENABLE=y",
]
for setting in required_sdk:
    assert setting in sdk, f"missing sleep setting: {setting}"

# Bench diagnostics must be visible on the ESP32-H2 native USB connector.
assert "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y" in sdk
assert "# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set" in sdk
assert "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y" in sdk
assert "CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y" in sdk

assert "ezb_nwk_set_rx_on_when_idle(false);" in main
# Reliability regression: automatic MCU light sleep breaks parent polling on
# battery-only ESP32-H2 builds; keep the ZED receiver-off behavior but leave
# the CPU awake so the Zigbee stack can perform its 4-second keep-alive polls.
assert ".light_sleep_enable = false" in main
assert ".light_sleep_enable = true" not in main
assert "gpio_wakeup_enable(CALIBRATION_BUTTON_GPIO" in main
assert "ulTaskNotifyTake(pdTRUE, portMAX_DELAY);" in main
assert "ESP_PM_NO_LIGHT_SLEEP" in motor
assert "esp_pm_lock_acquire" in motor and "esp_pm_lock_release" in motor
assert "esp_zigbee_factory_reset();" in main
assert "SLEEPY_MIGRATION_KEY" in main
assert "EZB_ZDO_LEAVE_TYPE_RESET" in main
assert "heartbeat_task" not in led

print("sleep configuration tests: PASS")
