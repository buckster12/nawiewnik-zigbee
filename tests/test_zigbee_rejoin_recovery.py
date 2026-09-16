#!/usr/bin/env python3
from pathlib import Path

main = (Path(__file__).resolve().parents[1] / "main/main.c").read_text()

# A sleepy end device must stop claiming readiness and retry BDB
# initialization when its only parent/router disappears.
assert "case EZB_NWK_SIGNAL_NO_ACTIVE_LINKS_LEFT:" in main
assert "case EZB_NWK_SIGNAL_NETWORK_STATUS:" in main
assert "EZB_NWK_NETWORK_STATUS_PARENT_LINK_FAILURE" in main
assert "EZB_NWK_NETWORK_STATUS_LINK_FAILURE" in main
assert "s_zigbee_ready = false;" in main
assert "schedule_commissioning_retry(EZB_BDB_MODE_INITIALIZATION);" in main

# A burst of failure signals must not postpone recovery forever.
assert "esp_timer_is_active(s_commissioning_timer)" in main

# A reboot event is not enough by itself: confirm joined state before
# enabling reports and accepting control as online.
assert "else if (ezb_bdb_dev_joined())" in main

# Existing devices with a stale parent dataset get exactly one offline Zigbee
# reset per recovery revision. Versioning lets a corrected migration supersede
# an already-consumed older marker without creating a reset loop.
assert 'PARENT_RECOVERY_KEY "parent_fix"' in main
assert "PARENT_RECOVERY_VERSION 2" in main
assert "value != PARENT_RECOVERY_VERSION" in main
assert "nvs_set_u8(nvs, PARENT_RECOVERY_KEY, PARENT_RECOVERY_VERSION)" in main
assert "s_parent_recovery_required" in main
assert "mark_parent_recovery_complete()" in main
assert "s_sleepy_migration_required || s_parent_recovery_required" in main
assert "esp_zigbee_factory_reset();" in main
assert "ezb_bdb_reset_via_local_action();" not in main
failed_init = main.index("if (status != EZB_BDB_STATUS_SUCCESS)")
factory_reset = main.index("esp_zigbee_factory_reset();", failed_init)
retry = main.index("schedule_commissioning_retry(EZB_BDB_MODE_INITIALIZATION);", failed_init)
assert factory_reset < retry, "offline reset must run before retry on failed initialization"

print("Zigbee parent-loss recovery tests: PASS")
