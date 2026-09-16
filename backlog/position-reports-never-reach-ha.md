---
worth: yes
added: 2026-09-16
---
# reported lift position never reaches Home Assistant

The motor moves on command but HA's `current_position` does not follow. Observed 2026-09-16: a
`set_cover_position` to 50 landed (`Window command=0x05 payload=50 result=0x00`, HA position 50 at
09:52:44), then a close command ran the motor from 51% upward while HA stayed at 50 and never
updated again. The same split showed over the previous ~20 hours — `sensor.nawiewnik_battery` kept
updating hourly while `cover.nawiewnik` had not changed since a HA restart the day before.

The asymmetry points at the mechanism. Battery is pushed with an explicit
`ezb_zcl_report_attr_cmd_req` addressed to the coordinator, and it arrives. Position relies on the
stack's automatic configured reporting (`main/main.c:122` only sets the attribute value), and it does
not. So either Z2M never completed reporting configuration / binding for the Window Covering
cluster despite `configureReporting: true` in `zigbee2mqtt/nawiewnik-h2.mjs:13`, or the device side
never had it configured.

No `where` on purpose: whether this is a firmware bug or a converter/binding one is exactly the
unresolved part. Deliberately not "fixed" by adding a second explicit report — that path is banned
by `tests/test_report_frame_control.py`, which asserts exactly one report command exists, because
duplicating the stack's own report delivered positions out of order.

Next step is evidence, not a patch: read the device's reporting configuration and bindings from Z2M.
