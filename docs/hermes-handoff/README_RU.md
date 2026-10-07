# Передача работы Claude Code: батарея, питание, отчёты и OTA

Ветка создана от GitHub main d595526. База установленной прошивки d231dad теперь есть в истории; суффикс dirty в backup всё ещё требует проверки локальных незакоммиченных изменений на Mac.

## Что реально изменено в этой ветке
- battery_monitor.c: подтверждённая пользователем 07.10.2026 проводка battery+ -- 95k -- GPIO4 -- 300k -- GND, ADC_ATTEN_DB_12, top95000/bottom300000. Это отличается от исторической схемы docs/hardware.md (16.09). Перед прошивкой подтвердить текущую физическую схему; не менять её обратно по старому документу.
- regression test компилирует реальную формулу production и проверяет константы.
- OTA-кандидат приложен как reference patch, НЕ применён к production build/partition table. Нужны review, интеграция на актуальной базе и аппаратная приёмка. Бинарников и ключей сети в ветке нет.

## Новые данные GitHub: приоритеты и исправления старых гипотез
1. **Питание/brownout прежде позиционных отчётов.** docs/hardware.md содержит измерение260mV падения по плюсовой цепи holder->boost при движении. Сначала перепроверить актуальную сборку и силовую проводку/землю под нагрузкой, 5V/3V3, reset cause и ток. Не отключать brownout detector. Не паять непосредственно к18650 и не следовать устаревшему совету solder tabs из backlog вслепую. docs/hardware.md рекомендует убрать силовой ток с breadboard и проверить общий возврат. Где физически подключён GPIO3 motor-enable, неизвестно; software enable может не выключать питание.
2. **Разряд: retry backoff.** backlog/commissioning-retry-has-no-backoff.md фиксирует retry2s без увеличения интервала. Нужны тестируемый bounded backoff с reset после успешного join, контролируемая реакция на восстановление router и различение factory-new/rejoin. Потолок интервала согласовать. Не сбрасывать pairing/calibration, не оставлять device навсегда без retry. Light sleep сейчас намеренно отключён: исследовать причину и ток, не включать его одним флагом без тестов.
3. **BatteryVoltage reporting — отдельный баг от ADC.** backlog/battery-voltage-report-rejected.md: attr0x0020 return0x5, percentage attr0x0021 success. Перестать игнорировать ошибку. Проверить registration/access/reporting в текущем SDK; converter voltageReporting=true + Z2M reconfigure — гипотеза, не доказанный fix. Не объявлять напряжение свежим по одному cached HA state.
4. **Position reporting:** обновлённый backlog уже подтверждает binding и configured reporting. Повторный совет просто настроить binding недостаточен. Уточнить return reporting_start_attr_report, фактические ZCL frames, reset во время движения. Не дублировать explicit+automatic reports без решения out-of-order проблемы и обновления regression.
5. **Motor task/Zigbee lock:** bounded callback или очередь/coalescing возможны после воспроизведения; не блокировать stepping на portMAX_DELAY. Обеспечить final report retry и STOP, не утверждать, что timeout сам гарантирует финальный report. Наблюдение одно, power reset отличать от stall по uptime/reset cause.
6. **Recovery:** INITIALIZATION retry для сохранённой сети правильнее factory reset. Для factory-new/unusable dataset нужен отдельный ограниченный путь steering без стирания калибровки.
7. **Availability/LED:** отдельные задачи. Backlog availability содержит исторические Z2M настройки, их читать заново прежде HA changes. У sleepy device нужна passive/offline semantics, не включать indiscriminate polling всей сети. LED только с measured power budget и ограниченным временем/яркостью.

## Сохранить
Актуальные motor/endstop/boot homing/STOP, GPIO и calibration. USB-only boot/homing не использовать для моторной приёмки без проверки силового питания. Current_position в HA должна быть подтверждённой, а не только commanded echo.

## OTA и миграция
См. ota-candidate.patch и REFERENCE_OTA_HANDOFF.md. Patch рассчитан на код после включённого battery fix; не применять reference без анализа. Прежний generated sdkconfig для второго build environment намеренно не приложен — генерировать из актуального проекта.
Подтверждённая backup разметка: nvs0x9000/0x6000, factory0x10000/0x160000, zb_fct0x170000/0x1000. Candidate: ota_0 на старом factory диапазоне, новые otadata0x180000/0x2000 и ota_1 0x190000/0x160000; nvs/zb_fct не перемещать/перезаписывать. Никакого erase-flash/merged padding/обычного upload. Перед первой загрузкой нужен readback и сравнение сохраняемых диапазонов. Bootloader compatibility review обязателен.
Нет hardware-proven OTA, publisher signature или crash rollback. Включение genOta/ota:true/re-interview в Z2M отдельно согласовать. Факт успешной компиляции не означает wireless functionality. Приёмка — реальный transfer новой версии, reboot+currentFileVersion, interruption, данные/батарея/мотор/STOP.
Полная backup есть только на Mac пользователя и содержит возможные Zigbee secrets — никогда не добавлять её в git.

## Будущее охлаждение
Это отдельный HA этап: одна комнатная цель, heating/cooling interlock, наружная температура/эффективность, frost/overcool/stale/offline guards, hysteresis/manual override. Комната, sensor и cooling thresholds ещё не согласованы. Не менять действующую room heating систему в рамках firmware patch.

## Порядок
Проверить текущие hardware/local source -> ADC tests/build -> питание и ток/retry -> отчёты/recovery -> review и отдельная OTA интеграция -> согласованный wired bootstrap и wireless acceptance. Каждую гипотезу отделять от измеренного факта. Не push main, не прошивать, не менять HA/Z2M без явного разрешения.
