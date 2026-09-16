---
worth: yes
where: main/main.c:86
added: 2026-09-16
---
# battery voltage report is rejected by the stack and the error is discarded

`report_battery()` sends an explicit report for both power-configuration attributes. Instrumented on
hardware 2026-09-16, the voltage one fails every single time and the percentage one succeeds:

```
DIAG report_attr_cmd_req(attr=0x0020) -> 0x5   # BatteryVoltage, error, every attempt
DIAG report_attr_cmd_req(attr=0x0021) -> 0x0   # BatteryPercentageRemaining, ok
```

The call site discards it — `(void)ezb_zcl_report_attr_cmd_req(&report);` — so this has been failing
on every hourly cycle, invisibly. `sensor.nawiewnik_voltage` in HA is stuck at 4000 from a restart
the day before while percentage updates normally.

It lines up exactly with a second probe, `ezb_zcl_reporting_info_find()` on the live device:

```
DIAG reporting[lift_percentage] = CONFIGURED
DIAG reporting[battery_voltage] = NOT CONFIGURED
DIAG reporting[battery_percent] = CONFIGURED
```

So the stack refuses `report_attr_cmd_req` for an attribute that has no reporting information. Note
both attributes are `false` in the converter's `m.battery({percentageReporting: false,
voltageReporting: false})`, yet percentage still has reporting info — so the difference comes from
what the library marks reportable by default, not from Z2M configuration.

Two candidate fixes, neither verified:
- converter: `voltageReporting: true` in `zigbee2mqtt/nawiewnik-h2.mjs`, then reconfigure the device
  in Z2M so reporting info gets created. No reflash, but needs Z2M access to apply and confirm.
- firmware: register BatteryVoltage with `EZB_ZCL_ATTR_ACCESS_REPORTING`.
  `ezb_zcl_power_config_cluster_desc_add_attr()` takes no access flags, so this needs a different
  registration path; `ezb_zcl_cluster_desc_add_manuf_attr()` has `attr_access` but is for
  manufacturer-specific attributes.

Independent of which fix wins, stop discarding the return value. A silent failure on every cycle is
what let this run for a day unnoticed.
