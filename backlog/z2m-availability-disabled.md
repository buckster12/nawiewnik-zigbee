---
worth: later
added: 2026-09-16
---
# Zigbee2MQTT availability tracking is off for this network

`zigbee2mqtt/bridge/info` shows `availability.enabled: false` and `advanced.last_seen: "disable"`.
That is why the damper sat off-network for roughly a day on 2026-09-15/16 while
`cover.nawiewnik` in HA still read `open`: nothing ever marked it unavailable.

It is the cheapest possible offline detection — no firmware change, no battery cost — and it would
make the HA alert automation trigger on a real availability signal instead of the current
workaround, which infers offline from the age of the last battery report.

`later` because it is not this repo's decision to make: availability is a Z2M-wide setting and
enabling it adds active polling for every battery device on the network, not just this one. What
would settle it: whether the other battery devices tolerate `availability.active` polling, or
whether `passive` alone is enough for a device that reports hourly.

Relates to [[led-blink-when-offline]] — if this lands, the LED covers only the case where someone is
physically standing at the device with no phone.
