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

Observed 2026-09-16 while instrumenting the reporting bug. Extra work added inside that critical
section stopped the damper after two steps mid-travel. The failure was completely silent — no error,
no `Target reached`, and the board stayed alive and responsive (uptime kept climbing and a later
`stop_cover` was logged normally from the Zigbee task). Reverting and reflashing the committed
firmware restored full movement in the same conditions: 52 position steps and
`Target reached: steps=512 lift=50%`.

Consequence if it ever happens in the field: the damper parks at an arbitrary position, the
persisted position is never written (that write is in the same loop, after motion ends), and nothing
reports a fault. The stored position then disagrees with the physical one until the next boot homing.

`later` because the trigger is not established, not because the risk is unclear. It only reproduced
with artificial work added inside the section; in the committed firmware the lock is held for two
attribute writes and is presumably short. What would settle it: whether the Zigbee task ever holds
that lock long enough to matter — during commissioning retries, a rejoin, or a parent-link failure,
all of which do hold it and all of which happen while a move may be in flight.

If it turns out to matter, the shape of the fix is a bounded `esp_zigbee_lock_acquire` with the
position update skipped on timeout: a dropped intermediate report costs nothing, since the final
position is always published, whereas a stalled motor costs the calibration's agreement with reality.

Related: [[position-reports-never-reach-ha]] touches the same function, so whoever fixes that should
not lengthen this critical section.
