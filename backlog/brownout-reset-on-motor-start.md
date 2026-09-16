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

## Power chain, reconstructed with the owner 2026-09-16

```
18650 --[spring holder]--> TPS61023 boost --> 5 V rail --+--> ESP32-H2-Zero 5V pin -> LDO -> 3V3
                                                          +--> ULN2003AN board -> 28BYJ-48
USB-C is connected at the same time; whether it feeds the same 5 V net is UNKNOWN.
```

The MCU and the motor share one rail, so loading it browns out the chip. A boost converter is a
constant-power load, so as its input sags it draws MORE input current, which deepens the sag — the
rail collapses rather than settling, which matches the observed behaviour.

The arithmetic points at contact resistance, not at an undersized supply. A 5 V 28BYJ-48 is roughly
50 ohm per phase, so about 100 mA per coil and 100-200 mA at the 5 V output in half-step. That is
~0.22-0.30 A drawn from a 3.85 V cell at ~87% efficiency. A 370 mV sag at 0.25 A implies about
1.5 ohm of series resistance, an order of magnitude more than a healthy cell plus wiring should
show. A 28BYJ-48 should not be able to brown out an 18650 at all.

Coil resistance is the typical part figure, NOT measured here, so the current estimate rests on it.
Two measurements would replace the whole calculation: resistance from cell terminal to converter
input with the power off (anything above tenths of an ohm is the fault), and current in series with
the cell during a move.

Ranked remedies: eliminate the spring contacts first (solder tabs), then separate the MCU supply
from the motor supply. Half-step drive could be dropped to wave drive in
`motor_model_phase_mask()` to roughly halve motor current, at the cost of torque and resolution —
a firmware lever worth keeping only if the hardware cannot be changed.

No `where`: the defect is electrical, not a line of code. `motor_driver.c` enabling the rail is
where the load appears, not what is wrong. What to investigate: cell condition and internal
resistance under load, input bulk capacitance on the boost converter, and whether the MCU should be
fed separately from the motor rail. The firmware already staggers the rail by
`MOTOR_POWER_STARTUP_MS` (20 ms) before driving a phase, so the inrush is the converter's, not a
coil's.
