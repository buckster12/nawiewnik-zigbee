---
worth: yes
where: main/main.c:297
added: 2026-09-16
---
# commissioning retries never back off, so a lost network drains the cell

`schedule_commissioning_retry()` arms a one-shot timer at a fixed 2 s
(`esp_timer_start_once(s_commissioning_timer, 2000000)`) and the handler re-arms on every failure.
There is no backoff, no cap, and no give-up. A device that cannot find its network scans the radio
every two seconds for as long as it has power.

Measured 2026-09-16. The damper fell off the network at 14:32 UTC and was still looping
`Zigbee initialization failed: 0x03` at 17:50 — over three hours, on the order of 5,600 scans.
Battery over the same afternoon: 77% at 12:22, 67% at 12:31, 63% at 14:32.

This compounds two decisions that are each defensible alone. Automatic light sleep is deliberately
off (see `tests/test_sleep_configuration.py`) so the CPU never idles down, and the retry keeps the
radio scanning on top of that. On a cell-powered device the combination turns "out of radio range"
into "flat battery", and the failure is invisible: no report goes out, so nothing downstream can
tell the difference between a busy device and a dead one.

From the outside it reads as the device having gone to sleep. It is the opposite.

The fix is ordinary exponential backoff with a ceiling — retry at 2 s, then widen to something like
a minute or two and stay there. A device whose parent is gone is not going to find it by asking
five times a minute for three hours. Rejoin latency after the router returns would grow to at most
the ceiling, which is a fair trade against the cell.

Note this interacts with [[steering-unreachable-after-failed-init]]: both live in the same failure
branch, so whoever changes the retry policy should settle the steering fallback in the same pass.
