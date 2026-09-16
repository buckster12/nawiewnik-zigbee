# Nawiewnik Zigbee

Native ESP32-H2 Zigbee firmware for a battery-powered window-covering/vent actuator.

## Features

- Standard Zigbee Window Covering cluster (`0x0102`)
- Open, close, stop, and lift-percentage commands
- Persistent motor calibration, position, and speed in NVS
- Battery voltage and percentage reporting
- Parent-loss recovery and same-IEEE rejoin support
- Host-side regression tests for motor, homing, reporting, sleep, and power sequencing

## Build

```sh
pio run
```

The application image is generated at:

```text
.pio/build/esp32-h2/firmware.bin
```

For an update using the existing partition layout, write only the application image at `0x10000`; do not erase NVS unless an intentional factory reset is required.

## Test

```sh
bash tests/run_tests.sh
```

## Hardware

Target: ESP32-H2 with a ULN2003-driven stepper actuator. Pin assignments and device constants are defined under `main/`.

## License

MIT — see [LICENSE](LICENSE).

## Safety

Do not hot-plug motor or ULN2003 wiring. Calibration and position are inferred unless a physical endstop is installed; verify short movements before commanding full travel.
