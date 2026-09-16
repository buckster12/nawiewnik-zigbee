#!/usr/bin/env python3
from pathlib import Path

source = Path(__file__).parents[1].joinpath("main/main.c").read_text()
blocks = source.split("ezb_zcl_report_attr_cmd_t report = {")[1:]
assert len(blocks) == 1, (
    f"expected only the explicit battery report; position reporting must be automatic, got {len(blocks)} blocks"
)
initializer = blocks[0].split("};", 1)[0]
assert ".dis_default_rsp = 1" in initializer, "battery report must disable ZCL default responses"
assert ".dst_addr.addr_mode = EZB_ADDR_MODE_SHORT" in initializer, (
    "battery report must use an explicit coordinator address; Power Configuration is not bound"
)
assert ".dst_addr.u.short_addr = 0x0000" in initializer, "battery report must target the coordinator"
assert ".dst_ep = 1" in initializer, "battery report must target the coordinator endpoint"
assert "BATTERY_REPORT_INTERVAL_MS (60U * 60U * 1000U)" in source, (
    "battery must be sampled and reported hourly"
)
position_fn = source.split("static void report_lift_percentage", 1)[1].split("static void motor_position_changed", 1)[0]
assert "ezb_zcl_report_attr_cmd_req" not in position_fn, (
    "position updater must not duplicate the Zigbee stack's configured automatic report"
)
print("report frame-control tests: PASS")
