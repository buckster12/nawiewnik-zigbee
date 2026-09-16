---
worth: yes
where: main/main.c:321
added: 2026-09-16
---
# network steering is unreachable when initialization fails

In the `DEVICE_FIRST_START` / `DEVICE_REBOOT` handler, a non-SUCCESS status retries
`EZB_BDB_MODE_INITIALIZATION` forever (`main/main.c:321`). `EZB_BDB_MODE_NETWORK_STEERING` — the only
mode that joins a network the device is not already provisioned for — sits inside the success branch
at `main/main.c:326`, behind `ezb_bdb_is_factory_new()`. A device that fails initialization can
therefore never steer.

This did NOT cause the 2026-09-16 outage, and that is worth recording so it is not misdiagnosed
again. There the dataset was intact and the device only needed to rejoin, which is what
`INITIALIZATION` is for: it looped `NO_NETWORK` purely because no router was in range, and it
recovered on the very next retry once one was, with no code change. The retry loop is correct for
rejoin.

The gap is the other case: a device whose dataset is gone or unusable has no path back, since the
one-shot `parent_fix` / `sleep_zed` factory-reset escape is spent after its first use. Reaching it
currently needs a manual NVS erase, which also destroys the motor calibration.

A fix must update `tests/test_zigbee_rejoin_recovery.py`, which asserts the literal
`schedule_commissioning_retry(EZB_BDB_MODE_INITIALIZATION);` and the reset-before-retry ordering.
