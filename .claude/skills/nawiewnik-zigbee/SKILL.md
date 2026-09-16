---
name: nawiewnik-zigbee
description: >-
  Device-specific invariants for the Nawiewnik ESP32-H2 Zigbee damper firmware in this repo: pin
  map, inverted lift semantics, motor power sequencing, sleepy-ZED constraints, the custom motor
  speed attribute, NVS persistence and recovery markers, and the source-string tests that guard
  main/. Use before editing main/, flashing this board, diagnosing homing/calibration, or touching
  Zigbee joining or the Zigbee2MQTT converter. For general ESP32/ESP-IDF/PlatformIO/GPIO/flashing
  technique use esp32-development; for root-causing a failure use systematic-debugging.
---

# Nawiewnik ESP32-H2 — project invariants

Battery ESP32-H2 driving a ULN2003 stepper damper, presented to Home Assistant as a Zigbee Window
Covering. This skill holds only what is true of *this* device. Generic ESP32 technique lives in
`esp32-development`; do not duplicate it here.

## Hardware

ESP32-H2, RISC-V, native USB-Serial/JTAG console. **Never carry a pin map over from a C3/C6 board** —
these assignments are H2-specific and verified against `main/motor_driver.c` and `main/main.c`.

| GPIO | Function |
| --- | --- |
| 10–13 | ULN2003 stepper phases A–D |
| 3 | 5 V motor rail enable |
| 2 | Reed/hall endstop, **active-low**, internal pull-up |
| 4 | Battery ADC (ADC1 ch3), 300k/95k divider, 2.5 dB atten |
| 8 | WS2812 status LED — blinks only to confirm calibration |
| 9 | BOOT button: hold 3 s to mark current position CLOSED; also a sleep wakeup source |

Do not hot-plug motor or ULN2003 wiring.

## Motor semantics — the lift percentage is inverted

- `current_steps == 0` → **CLOSED** → ZCL lift **100 %**
- `current_steps == travel_steps` → **OPEN** → ZCL lift **0 %**
- So `motor_driver_open()` calls `motor_driver_set_lift_percentage(0)`. Getting this backwards is
  the easiest bug to introduce here.
- While `calibrated == false`, every movement command returns ZCL `FAIL` and the reported position
  is `0xFF`.
- Defaults in NVS: `travel` 1024 (valid 100–8192), `speed` 50 steps/s (valid 10–200).

## Power sequencing — order is load-bearing

`test_motor_power_sequence.py` asserts this ordering by reading the source:

- Startup: enable rail → wait **20 ms** (`MOTOR_POWER_STARTUP_MS`) → drive phases.
- Shutdown: de-energize phases → wait **2 ms** (`MOTOR_POWER_SHUTDOWN_MS`) → disable rail.
- `motor_driver_init()` must leave the rail explicitly disabled.

## Boot homing blocks for up to ~31 s

`motor_driver_init()` runs `motor_driver_home_open()` **synchronously, before the Zigbee task
starts**: steps toward OPEN at 50 steps/s (20 ms/step) until GPIO2 reads low for 3 debounced
samples, budget `travel * 3 / 2` = 1536 steps by default.

"Nothing happens for 30 seconds after reset, then it joins" is correct behavior. "It never joins" —
first ask whether the motor moves during that window.

On failure: `E motor: Open homing failed after N steps; position invalid` → position invalidated,
all motion rejected. Recovery is the endstop/magnet, or close the damper by hand and hold BOOT 3 s.

## Zigbee invariants

- Endpoint **10**, Window Covering `0x0102`, plus Basic and Power Configuration.
- Channel mask `0x07FFF800` (channels 11–26).
- Sleepy ZED: `rx_on_when_idle = false`, `keep_alive` 4000 ms, `ed_timeout` 64 min. The parent
  buffers commands, so **second-plus command latency is normal**, not a bug to chase.
- **MCU automatic light sleep MUST remain disabled** (`.light_sleep_enable = false`). It broke the
  4 s keep-alive polls on battery-only builds and the device kept losing its parent.
  `test_sleep_configuration.py` asserts both that `false` is present and that `true` is absent. The
  motor additionally holds an `ESP_PM_NO_LIGHT_SLEEP` lock while moving.
- Position reporting is the stack's own configured reporting. `report_lift_percentage()` only sets
  the attribute — it must **never** send a report command. Moving updates are throttled to ~1 s;
  the final position always goes out.
- Battery is the exception: Z2M does not bind Power Configuration, so `report_battery()` sends an
  explicit report to the **coordinator, short `0x0000`, endpoint 1**, `dis_default_rsp = 1`.
  There must remain **exactly one** `ezb_zcl_report_attr_cmd_t` in `main.c` — the test counts them.
- ZCL units, not intuition: voltage is `mv/100`, percentage is `percent*2`.

### The API is `ezb_*`, not `esp_zb_*`

esp-zigbee-lib **2.0.4**. Nearly every example online is the 1.x `esp_zb_*` API and will not
compile. Grep `managed_components/` (exists only after a build) for real signatures.

### Custom attribute and Z2M converter

`zigbee2mqtt/nawiewnik-h2.mjs` is a separate artifact that must be copied into Z2M's
`data/external_converters/` and kept in sync by hand:

| Firmware | Converter |
| --- | --- |
| model `Nawiewnik-H2`, manufacturer `SantaRumor` | `zigbeeModel`, `vendor` |
| attr `0xF001` UINT16, manufacturer code `0x1234` | `attribute.ID`, `zigbeeCommandOptions.manufacturerCode` |
| speed range 10–200 | `valueMin` / `valueMax` |
| lift 100 % = closed | `coverInverted: false` |

The Basic strings are ZCL **length-prefixed**: `"\x0A" "SantaRumor"`, `"\x0C" "Nawiewnik-H2"`.
Renaming the model means updating the string, its length byte, and `zigbeeModel` together.
An `UNSUPPORTED_ATTRIBUTE` on the speed attribute almost always means a manufacturer-code mismatch.

## Persistence — preserve NVS

The Zigbee dataset lives in the **same `nvs` partition** as the motor calibration
(`NAWIEWNIK_ZB_STORAGE_PARTITION "nvs"`). Erasing it costs pairing *and* calibration at once.

- Routine update writes the app only, at **`0x10000`** (`idf.py app-flash`). Never `erase-flash`
  unless the user asked for a factory reset, and say what it costs first.
- `naw_motor`: `position`, `calibrated`, `travel`, `speed`.
- `naw_sys`: `sleep_zed` (sleepy migration) and `parent_fix` (= `PARENT_RECOVERY_VERSION`, now **2**).

Both markers drive **one-shot** offline `esp_zigbee_factory_reset()` migrations for devices carrying
a stale dataset; they clear only the Zigbee dataset, not the motor calibration. Bumping
`PARENT_RECOVERY_VERSION` lets a corrected migration supersede a consumed marker without a reset
loop. Markers are written *before* the reset so a failure cannot loop.

Seeing `Clearing stale Zigbee dataset once before a clean rejoin` on **every** boot means a marker
is not persisting — look at `mark_*_complete()`, not at the reset logic.

Recovery is debounced through a 2 s one-shot timer guarded by `esp_timer_is_active()`, so a burst of
failure signals cannot postpone recovery forever. `s_zigbee_ready` gates all reporting.

## Editing main/ — the tests read source text

The Python tests are **not** behavioral. They `assert` on literal strings, their absence, and their
relative order in `main/*.c` and `sdkconfig.defaults`. Renaming a symbol, reordering two lines, or
reformatting will fail them even when behavior is identical.

| Test | Guards |
| --- | --- |
| `test_homing_wiring.py` | endstop GPIO/pull-up/active-low, `motor_driver_home_open()` inside init |
| `test_motor_power_sequence.py` | rail enable/disable ordering, startup delay, rail off at init |
| `test_sleep_configuration.py` | sdkconfig sleep keys, console keys, `light_sleep_enable = false` |
| `test_report_frame_control.py` | exactly one report command, its address/frame control, hourly interval |
| `test_zigbee_rejoin_recovery.py` | recovery signals, marker versioning, reset-before-retry order |

When a change is deliberate, update the assertion **and** keep the comment explaining why the
constraint exists. The C tests (`motor_model`, `homing_controller`) are real unit tests — those two
modules are pure logic with no ESP-IDF dependency, which is why most behavior is verifiable without
a board.

## Verify

```sh
bash tests/run_tests.sh                        # host C tests + 4 of the 5 Python tests
python3 tests/test_zigbee_rejoin_recovery.py   # NOT in the runner — run it by hand
pio run                                        # or: idf.py build
```

`run_tests.sh` compiles with `cc`, so `sudo xcodebuild -license` must have been accepted. Neither
`pio` nor `idf.py` is installed on this machine yet — see `esp32-development` for toolchain setup.
