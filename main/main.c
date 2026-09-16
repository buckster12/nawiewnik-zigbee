#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "ezbee/zcl/cluster/power_config.h"
#include "ezbee/zcl/cluster/window_covering.h"

#include "battery_monitor.h"
#include "motor_driver.h"
#include "nawiewnik.h"
#include "status_led.h"

#define CALIBRATION_BUTTON_GPIO GPIO_NUM_9
#define CALIBRATION_HOLD_MS 3000
#define NAWIEWNIK_MANUFACTURER_CODE 0x1234
#define NAWIEWNIK_ATTR_MOTOR_SPEED 0xF001
#define SYSTEM_NVS_NAMESPACE "naw_sys"
#define SLEEPY_MIGRATION_KEY "sleep_zed"
#define PARENT_RECOVERY_KEY "parent_fix"
#define PARENT_RECOVERY_VERSION 2

static const char *TAG = "nawiewnik";
static uint8_t s_lift_percentage = 0xFF;
static uint16_t s_motor_speed;
static bool s_zigbee_ready;
static esp_timer_handle_t s_commissioning_timer;
static ezb_bdb_comm_mode_t s_commissioning_retry_mode;
static int64_t s_last_position_report_us;
static uint8_t s_battery_voltage = 0xFF;
static uint8_t s_battery_percentage = 0xFF;
static TaskHandle_t s_calibration_button_task;
static bool s_sleepy_migration_required;
static bool s_parent_recovery_required;

#define POSITION_REPORT_INTERVAL_US 1000000LL
#define BATTERY_REPORT_INTERVAL_MS (60U * 60U * 1000U)

static void report_battery(void)
{
    if (!s_zigbee_ready) {
        return;
    }

    const uint16_t attrs[] = {
        EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
        EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
    };

    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_zcl_set_attr_value(NAWIEWNIK_ENDPOINT, EZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                           EZB_ZCL_CLUSTER_SERVER, attrs[0], EZB_ZCL_STD_MANUF_CODE,
                           &s_battery_voltage, false);
    ezb_zcl_set_attr_value(NAWIEWNIK_ENDPOINT, EZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                           EZB_ZCL_CLUSTER_SERVER, attrs[1], EZB_ZCL_STD_MANUF_CODE,
                           &s_battery_percentage, false);
    for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); ++i) {
        ezb_zcl_report_attr_cmd_t report = {
            .cmd_ctrl = {
                .fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI,
                .fc.dis_default_rsp = 1,
                /* Battery reporting is intentionally not configured/bound by
                 * Zigbee2MQTT. Send the active report directly to the
                 * coordinator instead of relying on a nonexistent binding. */
                .dst_addr.addr_mode = EZB_ADDR_MODE_SHORT,
                .dst_addr.u.short_addr = 0x0000,
                .dst_ep = 1,
                .src_ep = NAWIEWNIK_ENDPOINT,
                .cluster_id = EZB_ZCL_CLUSTER_ID_POWER_CONFIG,
            },
            .payload = {.attr_id = attrs[i]},
        };
        (void)ezb_zcl_report_attr_cmd_req(&report);
    }
    esp_zigbee_lock_release();
}

static esp_err_t sample_battery(void)
{
    uint16_t battery_mv;
    ESP_RETURN_ON_ERROR(battery_monitor_read_mv(&battery_mv), TAG, "Battery sample failed");
    uint8_t percent = battery_monitor_percentage(battery_mv);
    s_battery_voltage = (uint8_t)((battery_mv + 50U) / 100U);
    s_battery_percentage = (uint8_t)(percent * 2U);
    ESP_LOGI(TAG, "Battery: %u mV, %u%%", battery_mv, percent);
    return ESP_OK;
}

static void battery_task(void *arg)
{
    (void)arg;
    while (true) {
        while (!s_zigbee_ready) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        /* Send a few startup samples: rejoin and converter startup can race. */
        for (int attempt = 0; attempt < 3 && s_zigbee_ready; ++attempt) {
            if (sample_battery() == ESP_OK) {
                report_battery();
            }
            vTaskDelay(pdMS_TO_TICKS(10000));
        }

        vTaskDelay(pdMS_TO_TICKS(BATTERY_REPORT_INTERVAL_MS));
    }
}

static void report_lift_percentage(uint8_t percentage)
{
    if (!s_zigbee_ready) {
        return;
    }

    esp_zigbee_lock_acquire(portMAX_DELAY);
    s_lift_percentage = percentage;
    ezb_zcl_set_attr_value(NAWIEWNIK_ENDPOINT, EZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
                           EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
                           EZB_ZCL_STD_MANUF_CODE, &s_lift_percentage, false);
    esp_zigbee_lock_release();
}

static void motor_position_changed(uint8_t lift_percentage, bool moving)
{
    ESP_LOGI(TAG, "Position: lift=%u%% moving=%d", lift_percentage, moving);

    // Reporting on every percentage change can enqueue tens of ZCL reports,
    // starving command default responses and making HA receive stale positions
    // out of order. Coalesce moving updates; always publish the final position.
    int64_t now = esp_timer_get_time();
    if (moving && s_last_position_report_us != 0 &&
        now - s_last_position_report_us < POSITION_REPORT_INTERVAL_US) {
        return;
    }
    s_last_position_report_us = now;
    report_lift_percentage(lift_percentage);
}

static void calibration_button_task(void *arg)
{
    (void)arg;
    gpio_config_t button = {
        .pin_bit_mask = 1ULL << CALIBRATION_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));
    ESP_ERROR_CHECK(gpio_wakeup_enable(CALIBRATION_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(30));
        if (gpio_get_level(CALIBRATION_BUTTON_GPIO) != 0) {
            continue;
        }

        int held_ms = 30;
        while (gpio_get_level(CALIBRATION_BUTTON_GPIO) == 0 && held_ms < CALIBRATION_HOLD_MS) {
            vTaskDelay(pdMS_TO_TICKS(20));
            held_ms += 20;
        }
        if (held_ms >= CALIBRATION_HOLD_MS && gpio_get_level(CALIBRATION_BUTTON_GPIO) == 0) {
            motor_driver_mark_closed();
            status_led_confirm_calibration();
            ESP_LOGI(TAG, "Calibration button: current position marked CLOSED");
            while (gpio_get_level(CALIBRATION_BUTTON_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}

static void IRAM_ATTR calibration_button_isr(void *arg)
{
    (void)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_calibration_button_task, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static esp_err_t configure_power_management(void)
{
    const int cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    esp_pm_config_t config = {
        .max_freq_mhz = cpu_freq_mhz,
        .min_freq_mhz = cpu_freq_mhz,
        .light_sleep_enable = false,
    };
    ESP_RETURN_ON_ERROR(esp_pm_configure(&config), TAG, "Power management configuration failed");
    ESP_LOGI(TAG, "Automatic light sleep disabled for reliable Zigbee parent polling");
    return ESP_OK;
}

static esp_err_t load_sleepy_migration_state(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "System NVS open failed");
    uint8_t migrated = 0;
    esp_err_t err = nvs_get_u8(nvs, SLEEPY_MIGRATION_KEY, &migrated);
    nvs_close(nvs);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    s_sleepy_migration_required = migrated != 1;
    return ESP_OK;
}

static esp_err_t mark_sleepy_migration_complete(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "System NVS open failed");
    esp_err_t err = nvs_set_u8(nvs, SLEEPY_MIGRATION_KEY, 1);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        s_sleepy_migration_required = false;
    }
    return err;
}

static esp_err_t load_parent_recovery_state(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "System NVS open failed");
    uint8_t value = 0;
    esp_err_t err = nvs_get_u8(nvs, PARENT_RECOVERY_KEY, &value);
    nvs_close(nvs);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    s_parent_recovery_required = value != PARENT_RECOVERY_VERSION;
    return ESP_OK;
}

static esp_err_t mark_parent_recovery_complete(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "System NVS open failed");
    esp_err_t err = nvs_set_u8(nvs, PARENT_RECOVERY_KEY, PARENT_RECOVERY_VERSION);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        s_parent_recovery_required = false;
    }
    return err;
}

static void commissioning_retry_timer(void *arg)
{
    (void)arg;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    if (!ezb_bdb_dev_joined()) {
        (void)ezb_bdb_start_top_level_commissioning(s_commissioning_retry_mode);
    }
    esp_zigbee_lock_release();
}

static void schedule_commissioning_retry(ezb_bdb_comm_mode_t mode)
{
    s_commissioning_retry_mode = mode;
    if (s_commissioning_timer == NULL) {
        esp_timer_create_args_t args = {
            .callback = commissioning_retry_timer,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "zb_retry",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_commissioning_timer));
    } else if (esp_timer_is_active(s_commissioning_timer)) {
        return;
    }
    ESP_ERROR_CHECK(esp_timer_start_once(s_commissioning_timer, 2000000));
}

static bool zigbee_signal_handler(const ezb_app_signal_t *signal)
{
    ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);

    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal));
        if (status != EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "Zigbee initialization failed: 0x%02x", status);
            if (s_parent_recovery_required) {
                ESP_ERROR_CHECK(mark_sleepy_migration_complete());
                ESP_ERROR_CHECK(mark_parent_recovery_complete());
                ESP_LOGI(TAG, "Clearing stale Zigbee dataset after failed parent rejoin");
                esp_zigbee_factory_reset();
            }
            schedule_commissioning_retry(EZB_BDB_MODE_INITIALIZATION);
            break;
        }
        if (ezb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "Factory-new device; starting network steering");
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        } else if (s_sleepy_migration_required || s_parent_recovery_required) {
            ESP_ERROR_CHECK(mark_sleepy_migration_complete());
            ESP_ERROR_CHECK(mark_parent_recovery_complete());
            ESP_LOGI(TAG, "Clearing stale Zigbee dataset once before a clean rejoin");
            esp_zigbee_factory_reset();
        } else if (ezb_bdb_dev_joined()) {
            s_zigbee_ready = true;
            ESP_LOGI(TAG, "Rejoined saved Zigbee network");
        } else {
            s_zigbee_ready = false;
            ESP_LOGW(TAG, "Saved Zigbee network is unavailable; retrying secure rejoin");
            schedule_commissioning_retry(EZB_BDB_MODE_INITIALIZATION);
        }
    } break;

    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ESP_ERROR_CHECK(mark_sleepy_migration_complete());
            ESP_ERROR_CHECK(mark_parent_recovery_complete());
            s_zigbee_ready = true;
            ESP_LOGI(TAG, "Joined Zigbee network: PAN=0x%04hx channel=%u short=0x%04hx",
                     ezb_nwk_get_panid(), ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
        } else {
            ESP_LOGW(TAG, "Network steering failed: 0x%02x", status);
            schedule_commissioning_retry(EZB_BDB_MODE_NETWORK_STEERING);
        }
    } break;

    case EZB_ZDO_SIGNAL_LEAVE: {
        const ezb_zdo_signal_leave_params_t *params = ezb_app_signal_get_params(signal);
        s_zigbee_ready = false;
        ESP_LOGI(TAG, "Left Zigbee network (type=%u)", params ? params->leave_type : 0xFF);
        if (params != NULL && params->leave_type == EZB_ZDO_LEAVE_TYPE_RESET) {
            esp_restart();
        }
    } break;

    case EZB_NWK_SIGNAL_NO_ACTIVE_LINKS_LEFT:
        s_zigbee_ready = false;
        ESP_LOGW(TAG, "No active Zigbee parent links; retrying secure rejoin");
        schedule_commissioning_retry(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_NWK_SIGNAL_NETWORK_STATUS: {
        const ezb_nwk_signal_network_status_params_t *params = ezb_app_signal_get_params(signal);
        if (params != NULL &&
            (params->status == EZB_NWK_NETWORK_STATUS_LEGACY_LINK_FAILURE ||
             params->status == EZB_NWK_NETWORK_STATUS_LINK_FAILURE ||
             params->status == EZB_NWK_NETWORK_STATUS_PARENT_LINK_FAILURE)) {
            s_zigbee_ready = false;
            ESP_LOGW(TAG, "Zigbee parent/link failure 0x%02x; retrying secure rejoin", params->status);
            schedule_commissioning_retry(EZB_BDB_MODE_INITIALIZATION);
        }
    } break;

    default:
        ESP_LOGI(TAG, "Zigbee signal: %s (0x%02x)", ezb_app_signal_to_string(type), type);
        break;
    }
    return true;
}

static void window_covering_command_handler(ezb_zcl_window_covering_movement_message_t *message)
{
    if (message == NULL || message->in.header == NULL) {
        return;
    }
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
    uint8_t command = message->in.header->cmd_id;

    switch (command) {
    case EZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN_ID:
        if (!motor_driver_is_calibrated()) {
            message->out.result = EZB_ZCL_STATUS_FAIL;
        } else {
            motor_driver_open();
        }
        break;

    case EZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE_ID:
        if (!motor_driver_is_calibrated()) {
            message->out.result = EZB_ZCL_STATUS_FAIL;
        } else {
            motor_driver_close();
        }
        break;

    case EZB_ZCL_CMD_WINDOW_COVERING_STOP_ID:
        motor_driver_stop();
        break;

    case EZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE_ID:
        if (!motor_driver_set_lift_percentage(message->in.payload.lift_percentage)) {
            message->out.result = EZB_ZCL_STATUS_INVALID_VALUE;
        }
        break;

    default:
        message->out.result = EZB_ZCL_STATUS_UNSUP_CMD;
        break;
    }

    ESP_LOGI(TAG, "Window command=0x%02x payload=%u result=0x%02x", command,
             message->in.payload.lift_percentage, message->out.result);
}

static void zigbee_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id) {
    case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID: {
        ezb_zcl_set_attr_value_message_t *set = (ezb_zcl_set_attr_value_message_t *)message;
        if (set == NULL) {
            break;
        }
        set->out.result = EZB_ZCL_STATUS_SUCCESS;
        if (set->info.dst_ep == NAWIEWNIK_ENDPOINT &&
            set->info.cluster_id == EZB_ZCL_CLUSTER_ID_WINDOW_COVERING &&
            set->in.attribute.id == NAWIEWNIK_ATTR_MOTOR_SPEED) {
            if (set->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT16 ||
                set->in.attribute.data.size != sizeof(uint16_t) || set->in.attribute.data.value == NULL) {
                set->out.result = EZB_ZCL_STATUS_INVALID_TYPE;
                break;
            }
            uint16_t requested = *(uint16_t *)set->in.attribute.data.value;
            if (!motor_driver_set_speed(requested)) {
                set->out.result = EZB_ZCL_STATUS_INVALID_VALUE;
                break;
            }
            s_motor_speed = requested;
        }
    } break;
    case EZB_ZCL_CORE_WINDOW_COVERING_MOVEMENT_CB_ID:
        window_covering_command_handler((ezb_zcl_window_covering_movement_message_t *)message);
        break;
    case EZB_ZCL_CORE_DEFAULT_RSP_CB_ID:
        break;
    default:
        ESP_LOGW(TAG, "Unhandled ZCL callback: 0x%04lx", callback_id);
        break;
    }
}

static esp_err_t create_window_covering_endpoint(void)
{
    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_zha_window_covering_config_t config = EZB_ZHA_WINDOW_COVERING_CONFIG();
    config.basic_cfg.power_source = EZB_ZCL_BASIC_POWER_SOURCE_BATTERY;
    config.window_covering_cfg.window_covering_type = EZB_ZCL_WINDOW_COVERING_WINDOW_COVERING_TYPE_SHUTTER;
    config.window_covering_cfg.config_status = EZB_ZCL_WINDOW_COVERING_CONFIG_STATUS_OPERATIONAL |
                                                EZB_ZCL_WINDOW_COVERING_CONFIG_STATUS_ONLINE;
    config.window_covering_cfg.mode = EZB_ZCL_WINDOW_COVERING_MODE_LED_FEEDBACK;

    s_lift_percentage = motor_driver_get_lift_percentage();
    ezb_af_ep_desc_t endpoint = ezb_zha_create_window_covering(NAWIEWNIK_ENDPOINT, &config);
    ESP_RETURN_ON_FALSE(endpoint != NULL, ESP_FAIL, TAG, "Window covering endpoint creation failed");

    ezb_zcl_cluster_desc_t basic =
        ezb_af_endpoint_get_cluster_desc(endpoint, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_ERROR(ezb_zcl_basic_cluster_desc_add_attr(
                            basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)NAWIEWNIK_MANUFACTURER),
                        TAG, "Manufacturer attribute failed");
    ESP_RETURN_ON_ERROR(ezb_zcl_basic_cluster_desc_add_attr(
                            basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)NAWIEWNIK_MODEL),
                        TAG, "Model attribute failed");

    ezb_zcl_cluster_desc_t window = ezb_af_endpoint_get_cluster_desc(
        endpoint, EZB_ZCL_CLUSTER_ID_WINDOW_COVERING, EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_ERROR(ezb_zcl_window_covering_cluster_desc_add_attr(
                            window, EZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
                            &s_lift_percentage),
                        TAG, "Lift percentage attribute failed");
    s_motor_speed = motor_driver_get_speed();
    ESP_RETURN_ON_ERROR(ezb_zcl_cluster_desc_add_manuf_attr(
                            window, NAWIEWNIK_ATTR_MOTOR_SPEED, EZB_ZCL_ATTR_TYPE_UINT16,
                            EZB_ZCL_ATTR_ACCESS_READ_WRITE, NAWIEWNIK_MANUFACTURER_CODE, &s_motor_speed),
                        TAG, "Motor speed attribute failed");

    ezb_zcl_cluster_desc_t power =
        ezb_zcl_power_config_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_FALSE(power != NULL, ESP_FAIL, TAG, "Power configuration cluster creation failed");
    ESP_RETURN_ON_ERROR(ezb_zcl_power_config_cluster_desc_add_attr(
                            power, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &s_battery_voltage),
                        TAG, "Battery voltage attribute failed");
    ESP_RETURN_ON_ERROR(ezb_zcl_power_config_cluster_desc_add_attr(
                            power, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
                            &s_battery_percentage),
                        TAG, "Battery percentage attribute failed");
    ESP_RETURN_ON_ERROR(ezb_af_endpoint_add_cluster_desc(endpoint, power), TAG,
                        "Power configuration cluster registration failed");

    ESP_RETURN_ON_ERROR(ezb_af_device_add_endpoint_desc(device, endpoint), TAG, "Endpoint registration failed");
    ESP_RETURN_ON_ERROR(ezb_af_device_desc_register(device), TAG, "Device registration failed");
    ezb_zcl_core_action_handler_register(zigbee_action_handler);
    return ESP_OK;
}

static void zigbee_task(void *arg)
{
    (void)arg;
    esp_zigbee_config_t config = NAWIEWNIK_ZIGBEE_CONFIG();
    ESP_ERROR_CHECK(esp_zigbee_init(&config));
    /* A Zigbee End Device is not sleepy unless its receiver is explicitly
     * disabled while idle. The parent buffers commands until our next poll. */
    ezb_nwk_set_rx_on_when_idle(false);
    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(NAWIEWNIK_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(NAWIEWNIK_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(zigbee_signal_handler));
    ESP_ERROR_CHECK(create_window_covering_endpoint());
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();
    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_err);
    }

    ESP_ERROR_CHECK(status_led_init());
    ESP_ERROR_CHECK(battery_monitor_init());
    ESP_ERROR_CHECK(sample_battery());
    ESP_ERROR_CHECK(configure_power_management());
    ESP_ERROR_CHECK(load_sleepy_migration_state());
    ESP_ERROR_CHECK(load_parent_recovery_state());
    ESP_ERROR_CHECK(motor_driver_init(motor_position_changed));
    ESP_LOGI(TAG, "Starting native Zigbee Window Covering firmware");
    ESP_LOGI(TAG, "Calibration: close damper physically and hold BOOT for 3 seconds");

    ESP_ERROR_CHECK(xTaskCreate(calibration_button_task, "cal_button", 2048, NULL, 4,
                                &s_calibration_button_task) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    esp_err_t isr_service = gpio_install_isr_service(0);
    ESP_ERROR_CHECK(isr_service == ESP_OK || isr_service == ESP_ERR_INVALID_STATE
                        ? ESP_OK
                        : isr_service);
    ESP_ERROR_CHECK(gpio_isr_handler_add(CALIBRATION_BUTTON_GPIO, calibration_button_isr, NULL));
    ESP_ERROR_CHECK(xTaskCreate(zigbee_task, "zigbee", 6144, NULL, 5, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(battery_task, "battery", 3072, NULL, 3, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}
