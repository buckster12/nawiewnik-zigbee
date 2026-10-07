# Nawiewnik hardware

Reconstructed with the owner on 2026-09-16 while diagnosing brown-out resets. Follows the
`hardware-bringup` worksheet from the `esp32-development` skill. Values marked UNKNOWN are not
guesses to be filled in later by inference — they need a measurement or a look at the board.

## Target identity

| Item | Value |
|---|---|
| Board | Waveshare **ESP32-H2-Zero** (not the DevKitM-1 the `platformio.ini` board id names) |
| SoC | ESP32-H2, RISC-V, native USB-Serial/JTAG |
| Power source | Single 18650 cell in a **spring holder** |
| Construction | **Solderless breadboard with dupont jumpers — nothing soldered** |
| Charging | No charger on board; the only USB-C goes to the ESP32-H2-Zero, so the cell is charged externally |
| USB | USB-C, connected during bench work; whether it feeds the same 5 V net is UNKNOWN |

## Power chain

```
18650 --[spring holder]--> TPS61023 boost --> 5 V rail --+--> ESP32-H2-Zero 5V pin -> onboard LDO -> 3V3
   |                                                      +--> ULN2003AN board -> 28BYJ-48
   +--[95k]--+--[300k]--> GND        (divider midpoint -> GPIO4)
              |
            GPIO4
```

**The MCU and the motor share one 5 V rail.** That is the defect behind
`backlog/brownout-reset-on-motor-start.md`: loading the rail browns out the chip. A boost converter
is a constant-power load, so a sagging input makes it draw more input current, which deepens the sag
— the rail collapses instead of settling.

| Rail / source | Voltage | Consumers | Measured |
|---|---:|---|---|
| Cell | 3.0-4.2 V | TPS61023, sense divider | 3842 mV idle (old cell), 3975 mV idle (new), **3476 mV during a move** — see the divider note below |
| 5 V boost output | 5 V | ESP32-H2-Zero, ULN2003AN | UNKNOWN — not measured under load |
| Positive leg, holder+ to converter VIN | — | (wiring loss) | **10-11 mV idle, 260 mV during a move** |
| 3V3 | 3.3 V | ESP32-H2 | UNKNOWN |

The ~370 mV sag at an estimated 0.22-0.30 A implies roughly 1.5 ohm of series resistance, an order
of magnitude above a healthy cell plus wiring. The current is an estimate from the 28BYJ-48's
typical ~50 ohm per phase, **not measured**.

The whole circuit is on a solderless breadboard, which accounts for that number without needing a
faulty part anywhere. The cell-to-converter path crosses roughly eight to ten contacts — holder
spring, wire, dupont pin, breadboard clip, rail, clip, pin, wire — and dupont-to-header contacts run
10-100 mohm each while breadboard clips run 50-500 mohm, worse in a well-used hole. Eight contacts
averaging 150 mohm is ~1.2 ohm, which lands on the 1.5 ohm the voltage sag implies. Two independent
estimates agreeing is the strongest evidence here that the diagnosis is right.

So the spring holder is one contact among ten, not the main suspect. Earlier notes in this file and
in the backlog over-weighted it.

**Measured 2026-09-16 and it settles the question.** Probing along the POSITIVE leg alone — holder
plus terminal to converter `VIN`, both probes on the same conductor — reads 10-11 mV at idle and
**260 mV while the motor runs**. That single wire, excluding the ground return and excluding the
cell's own internal resistance, drops a quarter of a volt. At the estimated 0.25 A that is about
1 ohm in one leg, against a sane budget of under 0.05 ohm, so roughly twenty times over.

The idle figure is independently damning: at the 40-50 mA the board draws with the motor stopped,
10 mV already implies ~0.2 ohm.

This is what collapses the rail. The converter sees a quarter volt less than the cell provides, and
being a constant-power load it answers a lower input by drawing more current, which increases the
drop across this very wire.

**The divider was documented upside down until 2026-10-07.** Measured that day with a multimeter,
cell in the holder: 3.99 V across the cell and **2.8 V from cell minus to GPIO4**, a ratio of 0.70.
That matches 95k on top and 300k to ground (0.76), not the 300k/95k recorded earlier (0.24). The
firmware assumed the old orientation at 2.5 dB attenuation, whose range ends near 1 V, so GPIO4 sat
far above full scale and every firmware battery reading before that fix was the ADC's saturated
ceiling, not the cell. Any cell voltage or percentage above that came from the firmware — including
the 3838-3842 mV (51-53%) in `backlog/brownout-reset-on-motor-start.md` — is not a measurement.
Which of the three figures in the rail table were taken with a multimeter rather than read from the
firmware is not recorded.

The measured ratio is ~8% below the nominal one, so the fixed firmware will still read low (2.8 V on
the pin computes to ~3.69 V for a 3.99 V cell). Either the parts are off nominal or the meter loads
the divider; measuring each resistor out of the breadboard would settle it before adding a correction.

Note the sense divider returns to the cell's minus while the ADC measures against the MCU's ground.
If motor return current shares a ground path with resistance, part of the apparent sag is a ground
offset rather than a real cell droop. Both are the same class of fault; the location differs.

## Pin plan

Verified against the firmware and confirmed with the owner. ADC channel numbers are not GPIO
numbers: `ADC1_GPIO4_CHANNEL == 3` per `components/soc/esp32h2/include/soc/adc_channel.h`, which is
why `main/battery_monitor.c` uses `ADC_CHANNEL_3` for GPIO4.

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| Stepper phases A-D | 10, 11, 12, 13 | output | to ULN2003AN inputs |
| Motor rail enable | 3 | output, push-pull | **destination UNKNOWN** — the TPS61023 `EN` pin is not used |
| Reed / hall endstop | 2 | input, internal pull-up | other end to ground, active-low |
| Battery sense | 4 | ADC1 ch3, 12 dB | 95k top / 300k bottom divider across the cell |
| Status LED | 8 | RMT | on-board WS2812 |
| BOOT / calibration | 9 | input, pull-up | hold 3 s marks CLOSED; also a sleep wake source |

## Attached devices

| Device | Supply | Current | Driver / protection |
|---|---|---:|---|
| 28BYJ-48 stepper | 5 V rail | ~100 mA per coil; 100-200 mA in half-step | ULN2003AN board, flyback diodes present |
| Reed / hall endstop | — | — | internal pull-up, switches to ground |

Half-step drive alternates one and two energised coils (`motor_model_phase_mask()` masks
`0x01, 0x03, 0x02, 0x06, 0x04, 0x0C, 0x08, 0x09`). Wave drive would roughly halve motor current at
the cost of torque and resolution — a firmware lever, only worth taking if the hardware cannot be
changed.

## Known gaps

1. **Where GPIO3 actually goes.** The firmware sequences a motor rail (enable, wait 20 ms, drive
   phases; reverse on stop) and `tests/test_motor_power_sequence.py` guards that order. With the
   converter's `EN` unused, the pin may drive a MOSFET or may be unconnected. If unconnected, the
   5 V rail is permanently live and the whole sequence is ceremony with no physical effect.
2. **Whether USB 5 V reaches the same rail.** USB was connected through every brown-out, so either
   it does not reach this net or the boost holds the rail above it and it never conducts.
3. **Series resistance, located.** Cell to converter input with the power off; anything above
   tenths of an ohm is the fault. Measure the cell-minus to board-GND offset during a move as well:
   100-300 mV there means the ground return is the problem, not the spring contacts on the positive
   side.
4. **Actual motor current**, in series with the cell during a move. It replaces the estimate this
   whole analysis rests on.

## The cheap fix, before any rework

Take only the CURRENT path off the breadboard: cell to converter input, converter output to the
ULN2003, and the common negative between cell, converter, ULN2003 and board. Those three links carry
the motor current. Everything else — four phase signals, the reed, the divider midpoint — draws
microamps to milliamps and can stay on the breadboard indefinitely.

Route the common negative as a star from one point rather than transiting the breadboard rail, since
motor return current sharing those clips is what lifts the board's ground and skews the ADC reading.

Nothing needs soldering to the cell, and the cell stays swappable, which matters because there is no
on-board charger.
