# Native Zigbee OTA candidate — not hardware validated

Source: isolated `/root/nawiewnik-ota-worktree`, branch `native-zigbee-ota`, base `a80a47c`.
Artifacts: `/root/nawiewnik-firmware/native-zigbee-ota/manifest.json`.

## Verified locally
- Native ESP-IDF 5.5.4 / ESP Zigbee SDK 2.x ezbee headers compile and link real receive/check/apply callbacks.
- Host fragmented-container/parser rejection tests, packaging test, integration source checks and existing motor/homing/battery/sleep/network tests pass.
- Both PlatformIO environments built successfully: `esp32-h2` OTA version 1 and `esp32-h2-ota-v2` OTA version 2.
- Each application 608288 bytes; OTA slot 1441792 bytes. Version 2 OTA container 608350 bytes.
- ESP image-info validates H2 chip ID, checksum and appended SHA. Partition binary decoded with IDF tool.
- Motor driver/model, homing, battery code and network/sleep logic unchanged from a80a47c. Existing bounded OPEN boot-homing remains intentional.

## Hardware baseline mismatch — do not conceal
User backup reports running `d231dad-dirty`, not our base `a80a47c`. That revision is NOT present in this local repository, so equivalence/diff cannot be verified; dirty source is unavailable. This candidate preserves a80a47c behavior, not provably every behavior of the physical running image. Parent must disclose/resolve acceptance of that baseline before installation. Full 4MiB backup already taken on Mac; user-provided SHA256 `1eff9d9eb068071e309f457840b8910671fd3427ec14d28670cfe35967d396df` (not locally verified).

## Partition migration (verified source layout matches user's decoded backup)
|Partition|Old|New|
|---|---|---|
|nvs|0x9000 / 0x6000|UNCHANGED|
|factory / ota_0|0x10000 / 0x160000|same extent; subtype factory -> ota_0|
|zb_fct|0x170000 / 0x1000|UNCHANGED|
|otadata|absent|0x180000 / 0x2000|
|ota_1|absent|0x190000 / 0x160000|
New end 0x2f0000 fits user-verified 4MiB H2 rev1.2. No application, metadata or partition-table write overlaps nvs or zb_fct. Retain existing IDF5.5.4 bootloader (standard IDF bootloader already supports OTA); no bootloader migration/rollback feature is required by this candidate. Decode/check backup bootloader flags and security info before any write if unknown. Do not enable flash encryption / secure boot, burn eFuses, erase-flash, or use PlatformIO upload/merged factory image. Merged factory image padding WOULD destroy preserved partitions.

### macOS commands — USER EXECUTES ONLY after baseline approval
Assumes esptool5.2.0 installed and files downloaded to local working folder. Substitute actual backup filename. Board currently `/dev/cu.usbmodem1101`, in ROM download mode. Existing backup is sufficient; do not repeat whole-flash read unnecessarily.

```sh
# Optional initial discovery/backup if not already done
python3 -m esptool --chip esp32h2 --port /dev/cu.usbmodem1101 --before no-reset --after no-reset flash-id
python3 -m esptool --chip esp32h2 --port /dev/cu.usbmodem1101 --before no-reset --after no-reset get-security-info
python3 -m esptool --chip esp32h2 --port /dev/cu.usbmodem1101 --before no-reset --after no-reset read-flash 0 0x400000 full-backup.bin
shasum -a 256 full-backup.bin

# Before write: compare downloaded files to artifact manifest SHA256.
shasum -a 256 bootstrap-v1/firmware.bin bootstrap-v1/partitions.bin bootstrap-v1/ota_data_initial.bin
# First app + EMPTY metadata; keep ROM mode throughout.
python3 -m esptool --chip esp32h2 --port /dev/cu.usbmodem1101 --before no-reset --after no-reset write-flash --flash-size keep 0x10000 bootstrap-v1/firmware.bin 0x180000 bootstrap-v1/ota_data_initial.bin
# Switch partition table LAST, only after previous write/hash verification succeeded.
python3 -m esptool --chip esp32h2 --port /dev/cu.usbmodem1101 --before no-reset --after no-reset write-flash --flash-size keep 0x8000 bootstrap-v1/partitions.bin
python3 -m esptool --chip esp32h2 --port /dev/cu.usbmodem1101 --before no-reset --after no-reset verify-flash 0x10000 bootstrap-v1/firmware.bin 0x8000 bootstrap-v1/partitions.bin 0x180000 bootstrap-v1/ota_data_initial.bin
# Preservation proof before boot
python3 -m esptool --chip esp32h2 --port /dev/cu.usbmodem1101 --before no-reset --after no-reset read-flash 0x9000 0x6000 nvs-after.bin
python3 -m esptool --chip esp32h2 --port /dev/cu.usbmodem1101 --before no-reset --after no-reset read-flash 0x170000 0x1000 zb-fct-after.bin
python3 -c "from pathlib import Path; b=Path('full-backup.bin').read_bytes(); assert Path('nvs-after.bin').read_bytes()==b[0x9000:0xf000]; assert Path('zb-fct-after.bin').read_bytes()==b[0x170000:0x171000]; print('NVS and zb_fct identical')"
```
Do not boot USB-only with motor battery off: preserved boot-homing can consume its bounded budget without motion. With power removed, ensure real motor rail/battery and reed/magnet are safe before normal reset; boot will deliberately home OPEN. Reopen Mac serial after USB re-enumeration. Never interpret flashing hash as proof of working application.

Recovery: ROM mode plus original full backup restores original layout/app/data if required; before full restore back up any newer calibration/network state and confirm intent. Full restore overwrites NVS intentionally. No automatic crash rollback is enabled in this candidate; known-good inactive app remains present but a bad boot needs wired selection/recovery.

## Exact converter/provider changes — staged ONLY, not deployed
`zigbee2mqtt/nawiewnik-h2.mjs`: add top-level `ota: true,` after description, leave every extend unchanged. This matches current upstream converters. Older deployed converter versions may require older provider object API; installed Z2M version must be checked before deployment. Re-interview endpoint after bootstrap so output/client cluster `genOta` (0x0019) is discovered. Do not delete pairing/NVS merely to refresh interview.

Current Zigbee2MQTT supports per-request custom local OTA file/index or global index override. No cloud provider necessary; serve/container must be reachable by the Z2M process, not just the user's Mac. For current Z2M one can provide the file via frontend firmware upload, or MQTT `zigbee2mqtt/bridge/request/device/ota_update/update` payload `{"id":"<device>","url":"<Z2M-accessible path or URL>/nawiewnik-v2.ota"}`. No MQTT call/config change performed here. Alternatively `ota.zigbee_ota_override_index_location` points to custom index. Metadata: manufacturerCode4660, imageType1, fileVersion2, fileSize608350, URL to real .ota. Validate installed Z2M schema/version before using example.

Firmware uses standard 56-byte OTA header with no optional fields and exactly one tag0 full ESP application, 6-byte subelement header, manufacturer0x1234, imageType1, stackVersion2, version2. Rejects optional headers, extra elements, offsets/gaps/duplicates, oversize, mismatched identity/version and downgrade. esp_ota_end validates ESP image before setting boot partition. Abort/errors do not select candidate boot. FINISH schedules reboot after server countdown, not from receiving callback. App version2 must actually be compiled with `-DNAW_OTA_VERSION=2`; merely relabeling version1 container is invalid release practice. No firmware signing/publisher authentication is added; trust Zigbee network/coordinator and controlled release file. SHA is integrity, not authenticity.

## Required hardware evidence before calling OTA usable
1. First boot on real power topology, expected bounded OPEN homing and reed debounce/failure shutdown; motor GPIO10/11/12/13, boostEN3, active-low reed2 untouched.
2. Fresh saved-network rejoin and telemetry; calibration/NVS retained. Existing a80 recovery markers may reset stale Zigbee dataset once if absent; existing NVS init erase-on-error path is unchanged. No guarantee that corrupt NVS or older marker state remains joined.
3. Endpoint advertises OTA client, currentFileVersion1 and correct manufacturer/type; refreshed Z2M supports_ota becomes true.
4. Actual version1->version2 Zigbee transfer via coordinator, sequential reception, checked ESP image, boot selection, reboot and currentFileVersion2 readback. USB cable to coordinator not necessary, but reliable radio and adequate battery power are.
5. Interrupted download/power-loss before APPLY leaves old image bootable; malformed image fails without boot selection. Test while idle, no movement during download.
6. Post-update motor/STOP/position/battery/local button, bounded homing on reset, repeated network responsiveness on battery without USB data, and later idle commands. Automatic light sleep intentionally remains disabled as in base.

## Evidence / scope limits
Host integration test currently checks callback wiring/source, not emulation of proprietary SDK events or real flash. Parser/package tests execute actual code. Compilation alone proves none of OTA radio interoperability, countdown callback timing, power-loss durability, or bad-app recovery. No hardware flashed. No HA/Z2M/profile state changed.
Sources inspected: local managed_components ezbee OTA headers; Espressif OTA example fetched from official main; https://www.zigbee2mqtt.io/guide/usage/ota_updates.html and upstream third_reality.ts (`ota: true`).
