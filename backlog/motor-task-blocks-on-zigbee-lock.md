---
worth: later
where: main/main.c:128
added: 2026-09-16
---
# the motor task can block forever on the Zigbee lock, and a stall is silent

`motor_task` calls the position callback outside its own mutex, so the chain
`motor_task -> motor_position_changed -> report_lift_percentage` reaches
`esp_zigbee_lock_acquire(portMAX_DELAY)` at `main/main.c:128` on the motion path. With
`portMAX_DELAY` there is no timeout: if the Zigbee task holds that lock, stepping stops for as long
as it holds it.

CORRECTION 2026-09-16, same day: this item was filed on a bad premise and most of its original
evidence belongs to [[brownout-reset-on-motor-start]]. Every other "the motor stalled after two
steps" observation that day turned out to be the board resetting, not a task blocking.

What survives is one observation that the reset explanation does NOT fit: with instrumentation added
inside the critical section, the damper stopped after two steps while the uptime kept climbing
(24164 -> 73379 ms with no reboot) and a later `stop_cover` was logged normally from the Zigbee task.
A reset would have restarted the uptime. So something stopped the motor task alone while the rest of
the firmware stayed live — which is what the mechanism above would look like — but it is a single
unreproduced sighting made while the board was also brown-out prone.

Consequence if it ever happens in the field: the damper parks at an arbitrary position, the
persisted position is never written (that write is in the same loop, after motion ends), and nothing
reports a fault. The stored position then disagrees with the physical one until the next boot homing.

`later` because the evidence for it is now one sighting, not because the risk is unclear. What would
settle it: re-run the instrumented build AFTER the power problem is fixed, so a stalled motor can no
longer be confused with a reset, and check whether the uptime-keeps-climbing stall reproduces. If it
does not, delete this item rather than carrying it.

If it turns out to matter, the shape of the fix is a bounded `esp_zigbee_lock_acquire` with the
position update skipped on timeout: a dropped intermediate report costs nothing, since the final
position is always published, whereas a stalled motor costs the calibration's agreement with reality.

Related: [[position-reports-never-reach-ha]] touches the same function, so whoever fixes that should
not lengthen this critical section.
