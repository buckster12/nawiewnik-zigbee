---
worth: later
where: main/status_led.c:58
added: 2026-09-16
---
# blink the status LED while off the Zigbee network

Requested after the 2026-09-16 outage: the damper sat off-network for about a day and looked
completely normal from the outside. A blink would make that visible without opening a serial console.

No hardware work needed. The WS2812 on GPIO8 is on-board and already driven — `status_led_init()` is
`ESP_ERROR_CHECK`-wrapped in `app_main()` and the boot log shows lines that follow it, so the strip
initialises. `s_zigbee_ready` already carries the state to display.

The unresolved part is whether it is worth the power, which is why this is `later` rather than `yes`.
`tests/test_sleep_configuration.py:41` asserts `"heartbeat_task" not in led`, and that assert dates
to the initial commit `c29558a` — a periodic LED task was ruled out from the start on a battery
device, not removed after a failure. Nobody has measured what a bounded version actually costs.

What would settle it: a current measurement for short low-brightness flashes (order 30 ms every 10 s)
bounded to the first few minutes after boot, against the cell's capacity and expected service
interval. If that is negligible, this becomes `yes` and the assert gets updated deliberately with a
comment recording the new bound. A complementary HA automation already alerts when battery reports
stop for 2 hours, so the LED only has to cover the case where someone is standing at the device.
