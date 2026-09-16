#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build-host"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT/main" \
  "$ROOT/tests/test_motor_model.c" \
  "$ROOT/main/motor_model.c" \
  -o "$ROOT/build-host/test_motor_model"
"$ROOT/build-host/test_motor_model"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT/main" \
  "$ROOT/tests/test_homing_controller.c" \
  "$ROOT/main/homing_controller.c" \
  -o "$ROOT/build-host/test_homing_controller"
"$ROOT/build-host/test_homing_controller"
python3 "$ROOT/tests/test_homing_wiring.py"
python3 "$ROOT/tests/test_report_frame_control.py"
python3 "$ROOT/tests/test_sleep_configuration.py"
python3 "$ROOT/tests/test_motor_power_sequence.py"
