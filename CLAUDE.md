# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Native Zigbee End Device firmware for an ESP32-H2 driving a ULN2003 stepper that opens/closes a
ventilation damper ("nawiewnik"). Battery powered, exposed to Home Assistant through Zigbee2MQTT as
a Window Covering.

## Skills carry the details

Three skills are installed; prefer them over re-deriving anything:

- **`nawiewnik-zigbee`** — this device's invariants: pin map, the inverted lift semantics, motor
  power timings, sleepy-ZED constraints, NVS layout and recovery markers, Z2M converter sync, and
  which literal strings each test guards. **Read it before editing `main/` or flashing.**
- **`esp32-development`** — general ESP32/ESP-IDF/PlatformIO technique, toolchain setup, flashing
  and serial debugging.
- **`systematic-debugging`** — root-cause protocol for any failure here.

Concrete numbers, GPIOs, offsets, and procedures live in the skills, not in this file. If the two
ever disagree, the source is the authority — fix both.

## Commands

```sh
pio run                      # build (or: idf.py build); image at .pio/build/esp32-h2/firmware.bin
bash tests/run_tests.sh      # host test suite, no hardware needed
```

`tests/test_zigbee_rejoin_recovery.py` exists but is **not** in `run_tests.sh` — run it by hand after
touching the Zigbee signal handler. The runner compiles with `cc`, so `sudo xcodebuild -license`
must have been accepted. Single tests:

```sh
python3 tests/test_sleep_configuration.py
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_motor_model.c main/motor_model.c -o build-host/t && build-host/t
```

Neither `pio` nor `idf.py` is installed on this machine yet.

## Architecture

The design deliberately separates logic that can be tested on a laptop from logic that needs a board:

- `motor_model.c` and `homing_controller.c` are **pure state machines** with no ESP-IDF dependency —
  step counting, half-step phase sequencing, percentage mapping, and bounded endstop search. They
  compile natively and carry real unit tests. Most behavior is verifiable without hardware because
  of this split; keep new logic on this side of the line whenever possible.
- `motor_driver.c` wraps them in hardware and FreeRTOS: GPIO, NVS persistence, the motor task, the
  power-management lock.
- `main.c` owns the Zigbee stack, endpoint and cluster construction, ZCL command handling, the
  battery task, and the calibration button.
- `battery_monitor.c` and `status_led.c` are leaf modules.

### Why the tests look strange

The C tests are ordinary unit tests. The **Python tests are source-text assertions**: they read
`main/*.c` and `sdkconfig.defaults` and assert that exact literal strings are present, absent, or
ordered. They exist because each one encodes a hardware or reliability bug that was expensive to
find — motor rail sequencing, disabled MCU light sleep, report frame control, recovery-marker
versioning — and that a behavioral test on a host cannot reach.

The cost is that renaming a symbol, reordering two lines, or reformatting `main/` fails them even
when behavior is unchanged. That is intended friction, not a broken test. When a change is
deliberate, update the assertion **and** keep the comment explaining why the constraint exists.

### Load-bearing decisions

Three choices look like mistakes and are not. Each has a test defending it, and each should be
changed only after reproducing the original failure on hardware:

- **MCU automatic light sleep is disabled** even though this is a battery device — it broke the
  Zigbee keep-alive polls and the device kept losing its parent.
- **Position and battery report through different mechanisms** — position via the stack's automatic
  reporting, battery via one explicit command, because Z2M binds one and not the other.
- **Boot homing runs synchronously before Zigbee starts**, which delays joining by up to half a
  minute. It is what establishes an absolute position reference.

### Zigbee API

esp-zigbee-lib **2.0.x**: `ezb_*` / `esp_zigbee_*`. Nearly every example online is the 1.x
`esp_zb_*` API and will not compile here. Grep `managed_components/` for real signatures rather than
porting from a 1.x example.

## Keeping things in sync

`zigbee2mqtt/nawiewnik-h2.mjs` is an external Zigbee2MQTT converter living in this repo. It is not
auto-generated and not auto-deployed: changing clusters, attributes, the model string, or the speed
range in the firmware means updating it by hand. The `nawiewnik-zigbee` skill has the field-by-field
map.

`sdkconfig.defaults` holds intentional choices and is asserted by the tests. `sdkconfig.esp32-h2` is
generated — rebuild rather than editing it.
