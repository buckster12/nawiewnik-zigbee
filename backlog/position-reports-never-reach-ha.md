---
worth: yes
where: main/main.c:122
added: 2026-09-16
---
# Home Assistant shows the commanded position, never the reported one

HA's `current_position` only ever echoes the last commanded value; the device's own position never
arrives. Proven on hardware 2026-09-16 by picking a command that carries no percentage, so an echo
is impossible:

- `cover.set_cover_position` to 40 -> HA showed 40 at 10:24:41 immediately.
- `cover.open_cover` -> `Window command=0x00 payload=0 result=0x00`, motor ran the full travel, and
  HA still showed 40 from 10:24:41 afterwards.

That also explains the earlier confusion: every HA position change so far had been an echo, and the
day-long "stale" stretch was simply a period with no positional commands.

The firmware side looks correct, which is what makes this hard. On the live device
`ezb_zcl_reporting_info_find()` reports `reporting[lift_percentage] = CONFIGURED`, and
`ezb_zcl_set_attr_value()` for the attribute returns `0x0`. Calling
`ezb_zcl_reporting_start_attr_report()` on that handle explicitly changed nothing.

Deliberately NOT "fixed" by adding an explicit report next to the attribute write:
`tests/test_report_frame_control.py` asserts exactly one `ezb_zcl_report_attr_cmd_t` exists and that
`report_lift_percentage` contains no `ezb_zcl_report_attr_cmd_req`, because duplicating the stack's
own report delivered positions out of order.

The Z2M side was checked and is correct. Read over MQTT (subscribe through Home Assistant's
websocket `mqtt/subscribe`, so no broker password is needed), `bridge/devices` gives for endpoint 10:

```
bindings: closuresWindowCovering -> 0x00124b002c3ad04a      # the coordinator
configured_reportings:
  closuresWindowCovering.currentPositionLiftPercentage  min=1 max=65000 delta=1
```

Binding present, reporting configured, delta 1 with a 1 s minimum. Nothing to fix there.

The radio link is fine too, which rules out the obvious excuse. Watching `zigbee2mqtt/Nawiewnik`
through a commanded move, Z2M keeps receiving traffic from the device — `linkquality` moves between
72 and 93 and `battery` ticked 54 -> 55 — while `position` never changes from the value that was
last commanded.

So both ends are configured correctly, the device is reachable, `ezb_zcl_set_attr_value()` returns
`0x0`, `ezb_zcl_reporting_info_find()` finds the info, and the report still never goes out. That
points at the firmware never actually emitting it: `main.c` makes no call into the reporting API at
all, and writing the attribute value alone is evidently not enough to trigger one. An explicit
`ezb_zcl_reporting_start_attr_report()` on the found handle changed nothing, though its return code
was not captured and is worth confirming before drawing a conclusion from it.

Not the same defect as [[battery-voltage-report-rejected]]: that one fails loudly with an error code
once instrumented, this one reports success at every step and still does not arrive.
