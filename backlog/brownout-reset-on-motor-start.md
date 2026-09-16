---
worth: yes
added: 2026-09-16
---
# the board brown-out resets when the motor starts, and that is the whole outage

Every movement command resets the MCU. Reproduced three times in a row on 2026-09-16, on the
committed firmware with no instrumentation:

```
I (25957) motor: New target: lift=100%
I (25957) nawiewnik: Window command=0x01 payload=0 result=0x00
=== USB PORT DISAPPEARED ===
W (2871) nawiewnik: Zigbee initialization failed: 0x03      <- uptime restarts from zero
```

The USB-Serial/JTAG device disappears from the host at the same instant, which is the chip dying
rather than a task stalling. `CONFIG_ESP_BROWNOUT_DET=y` is enabled and the cell measured
3838-3842 mV (51-53%) across the attempts, so the inrush through the 5 V converter feeding the
ULN2003 is pulling the rail under the detector threshold.

Confirmed physically by the owner watching the damper: it twitches a little toward closed, freezes,
then travels back to the magnet — that last leg being `motor_driver_home_open()` running on the
reboot that just happened.

This is the root cause of the original complaint, and it chains with the radio problem. Each attempt
to move takes the device off the network for the 25+ seconds it then spends retrying, and with no
mains router in range (see [[nawiewnik-needs-zigbee-router]]) it never gets back at all. That is why
the device had been re-paired eleven times over two days.

It also explains why position never reaches HA. With reporting configured at `min=1`, the first
report is due a second after the value changes, and the device resets before that elapses. The moves
that did complete were the ones whose sag missed the threshold. Fixing
[[position-reports-never-reach-ha]] on a board that resets on every move is not worth attempting
until this is resolved.

No `where`: the defect is electrical, not a line of code. `motor_driver.c` enabling the rail is
where the load appears, not what is wrong. What to investigate: cell condition and internal
resistance under load, input bulk capacitance on the boost converter, and whether the MCU should be
fed separately from the motor rail. The firmware already staggers the rail by
`MOTOR_POWER_STARTUP_MS` (20 ms) before driving a phase, so the inrush is the converter's, not a
coil's.
