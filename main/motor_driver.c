#include "motor_driver.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "homing_controller.h"
#include "motor_model.h"
#include "nvs.h"

#define MOTOR_PIN_A GPIO_NUM_10
#define MOTOR_PIN_B GPIO_NUM_11
#define MOTOR_PIN_C GPIO_NUM_12
#define MOTOR_PIN_D GPIO_NUM_13
#define MOTOR_POWER_ENABLE_GPIO GPIO_NUM_3
#define HALL_ENDSTOP_GPIO GPIO_NUM_2
#define MOTOR_POWER_STARTUP_MS 20
#define MOTOR_POWER_SHUTDOWN_MS 2
#define HOMING_DEBOUNCE_SAMPLES 3
#define HOMING_DEBOUNCE_MS 10
#define HOMING_SPEED_STEPS_PER_SECOND 50
#define HOMING_MAX_STEPS_MULTIPLIER 3
#define HOMING_MAX_STEPS_DIVISOR 2
#define MOTOR_DEFAULT_TRAVEL_STEPS 1024
#define MOTOR_DEFAULT_SPEED_STEPS_PER_SECOND 50
#define MOTOR_MIN_SPEED_STEPS_PER_SECOND 10
#define MOTOR_MAX_SPEED_STEPS_PER_SECOND 200
#define MOTOR_NVS_NAMESPACE "naw_motor"

static const char *TAG = "motor";
static const gpio_num_t s_pins[4] = {MOTOR_PIN_A, MOTOR_PIN_B, MOTOR_PIN_C, MOTOR_PIN_D};
static motor_model_t s_motor;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static motor_position_callback_t s_callback;
static nvs_handle_t s_nvs;
static uint16_t s_speed_steps_per_second = MOTOR_DEFAULT_SPEED_STEPS_PER_SECOND;
static esp_pm_lock_handle_t s_no_light_sleep_lock;
static bool s_no_light_sleep_lock_held;
static bool s_motor_power_enabled;

static void motor_apply_mask(uint8_t mask)
{
    for (size_t i = 0; i < 4; ++i) {
        gpio_set_level(s_pins[i], (mask >> i) & 0x01);
    }
}

static void motor_deenergize(void)
{
    motor_apply_mask(0);
}

static void motor_power_enable(void)
{
    if (s_motor_power_enabled) {
        return;
    }
    gpio_set_level(MOTOR_POWER_ENABLE_GPIO, 1);
    s_motor_power_enabled = true;
}

static void motor_power_disable(void)
{
    gpio_set_level(MOTOR_POWER_ENABLE_GPIO, 0);
    s_motor_power_enabled = false;
}

static void motor_save_position(int32_t current_steps, bool calibrated)
{
    if (s_nvs == 0) {
        return;
    }
    nvs_set_i32(s_nvs, "position", current_steps);
    nvs_set_u8(s_nvs, "calibrated", calibrated ? 1 : 0);
    nvs_set_i32(s_nvs, "travel", s_motor.travel_steps);
    nvs_commit(s_nvs);
}

static void motor_home_open(void)
{
    homing_controller_t homing;
    uint32_t max_steps = (uint32_t)s_motor.travel_steps * HOMING_MAX_STEPS_MULTIPLIER /
                         HOMING_MAX_STEPS_DIVISOR;
    homing_controller_init(&homing, max_steps, HOMING_DEBOUNCE_SAMPLES);
    motor_model_invalidate_position(&s_motor);

    bool success = false;
    bool power_enabled = false;
    ESP_ERROR_CHECK(esp_pm_lock_acquire(s_no_light_sleep_lock));
    ESP_LOGI(TAG, "Open homing started: GPIO%d NO-to-GND, max_steps=%lu", HALL_ENDSTOP_GPIO,
             (unsigned long)max_steps);

    while (true) {
        bool sensor_active = gpio_get_level(HALL_ENDSTOP_GPIO) == 0;
        homing_action_t action = homing_controller_update(&homing, sensor_active);
        if (action == HOMING_SUCCESS) {
            motor_model_mark_open(&s_motor);
            success = true;
            break;
        }
        if (action == HOMING_FAILED) {
            break;
        }
        if (action == HOMING_DEBOUNCE) {
            motor_deenergize();
            vTaskDelay(pdMS_TO_TICKS(HOMING_DEBOUNCE_MS));
            continue;
        }

        if (!power_enabled) {
            motor_power_enable();
            power_enabled = true;
            vTaskDelay(pdMS_TO_TICKS(MOTOR_POWER_STARTUP_MS));
        }
        uint8_t phase = 0;
        (void)motor_model_next_open_homing_step(&s_motor, &phase);
        motor_apply_mask(motor_model_phase_mask(phase));
        vTaskDelay(pdMS_TO_TICKS((1000U + HOMING_SPEED_STEPS_PER_SECOND - 1U) /
                                HOMING_SPEED_STEPS_PER_SECOND));
    }

    motor_deenergize();
    if (power_enabled) {
        vTaskDelay(pdMS_TO_TICKS(MOTOR_POWER_SHUTDOWN_MS));
        motor_power_disable();
    }
    ESP_ERROR_CHECK(esp_pm_lock_release(s_no_light_sleep_lock));

    if (success) {
        motor_save_position(s_motor.current_steps, true);
        ESP_LOGI(TAG, "Open homing complete: steps=%lu position=OPEN", (unsigned long)homing.steps_taken);
    } else {
        motor_model_invalidate_position(&s_motor);
        motor_save_position(s_motor.current_steps, false);
        ESP_LOGE(TAG, "Open homing failed after %lu steps; position invalid",
                 (unsigned long)homing.steps_taken);
    }
}

static void motor_task(void *arg)
{
    (void)arg;
    bool was_moving = false;
    uint8_t last_percentage = MOTOR_POSITION_UNKNOWN;

    while (true) {
        uint8_t phase = 0;
        uint8_t percentage;
        int32_t position;
        bool calibrated;
        uint16_t speed;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool stepped = motor_model_next_step(&s_motor, &phase);
        percentage = motor_model_current_lift_percentage(&s_motor);
        position = s_motor.current_steps;
        calibrated = s_motor.calibrated;
        speed = s_speed_steps_per_second;
        xSemaphoreGive(s_lock);

        if (stepped) {
            if (!s_motor_power_enabled) {
                motor_power_enable();
                vTaskDelay(pdMS_TO_TICKS(MOTOR_POWER_STARTUP_MS));
            }
            motor_apply_mask(motor_model_phase_mask(phase));
            was_moving = true;
            if (percentage != last_percentage && s_callback != NULL) {
                last_percentage = percentage;
                s_callback(percentage, true);
            }
            uint32_t step_delay_ms = (1000U + speed - 1U) / speed;
            vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
            continue;
        }

        motor_deenergize();
        if (s_motor_power_enabled) {
            vTaskDelay(pdMS_TO_TICKS(MOTOR_POWER_SHUTDOWN_MS));
            motor_power_disable();
        }
        if (was_moving) {
            was_moving = false;
            motor_save_position(position, calibrated);
            if (s_callback != NULL) {
                s_callback(percentage, false);
            }
            ESP_LOGI(TAG, "Target reached: steps=%ld lift=%u%%", (long)position, percentage);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_motor.current_steps == s_motor.target_steps && s_no_light_sleep_lock_held) {
            ESP_ERROR_CHECK(esp_pm_lock_release(s_no_light_sleep_lock));
            s_no_light_sleep_lock_held = false;
        }
        xSemaphoreGive(s_lock);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

esp_err_t motor_driver_init(motor_position_callback_t callback)
{
    uint64_t pin_mask = 0;
    for (size_t i = 0; i < 4; ++i) {
        pin_mask |= 1ULL << s_pins[i];
    }
    pin_mask |= 1ULL << MOTOR_POWER_ENABLE_GPIO;
    gpio_config_t io = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "GPIO configuration failed");
    gpio_config_t hall = {
        .pin_bit_mask = 1ULL << HALL_ENDSTOP_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&hall), TAG, "Reed endstop GPIO configuration failed");
    motor_deenergize();
    motor_power_disable();

    motor_model_init(&s_motor, MOTOR_DEFAULT_TRAVEL_STEPS);
    s_callback = callback;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "mutex allocation failed");

    esp_err_t err = nvs_open(MOTOR_NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "NVS open failed");

    int32_t travel = MOTOR_DEFAULT_TRAVEL_STEPS;
    int32_t position = 0;
    uint8_t calibrated = 0;
    uint16_t speed = MOTOR_DEFAULT_SPEED_STEPS_PER_SECOND;
    if (nvs_get_i32(s_nvs, "travel", &travel) != ESP_OK || travel < 100 || travel > 8192) {
        travel = MOTOR_DEFAULT_TRAVEL_STEPS;
    }
    motor_model_init(&s_motor, travel);
    (void)nvs_get_i32(s_nvs, "position", &position);
    (void)nvs_get_u8(s_nvs, "calibrated", &calibrated);
    if (nvs_get_u16(s_nvs, "speed", &speed) != ESP_OK ||
        speed < MOTOR_MIN_SPEED_STEPS_PER_SECOND || speed > MOTOR_MAX_SPEED_STEPS_PER_SECOND) {
        speed = MOTOR_DEFAULT_SPEED_STEPS_PER_SECOND;
    }
    s_speed_steps_per_second = speed;
    motor_model_restore(&s_motor, position, calibrated == 1);

    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "motor",
                                           &s_no_light_sleep_lock),
                        TAG, "Motor sleep lock creation failed");

    motor_home_open();

    BaseType_t created = xTaskCreate(motor_task, "motor", 3072, NULL, 6, &s_task);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "task creation failed");
    ESP_LOGI(TAG, "Initialized: travel=%ld position=%ld calibrated=%d speed=%u steps/s", (long)travel,
             (long)s_motor.current_steps, s_motor.calibrated, s_speed_steps_per_second);
    return ESP_OK;
}

bool motor_driver_set_lift_percentage(uint8_t lift_percentage)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool accepted = motor_model_set_lift_percentage(&s_motor, lift_percentage);
    if (accepted && s_motor.current_steps != s_motor.target_steps && !s_no_light_sleep_lock_held) {
        if (esp_pm_lock_acquire(s_no_light_sleep_lock) != ESP_OK) {
            motor_model_stop(&s_motor);
            accepted = false;
        } else {
            s_no_light_sleep_lock_held = true;
        }
    }
    xSemaphoreGive(s_lock);
    if (accepted) {
        xTaskNotifyGive(s_task);
        ESP_LOGI(TAG, "New target: lift=%u%%", lift_percentage);
    } else {
        ESP_LOGW(TAG, "Rejected target %u%%: calibration required", lift_percentage);
    }
    return accepted;
}

void motor_driver_open(void)
{
    (void)motor_driver_set_lift_percentage(0);
}

void motor_driver_close(void)
{
    (void)motor_driver_set_lift_percentage(100);
}

void motor_driver_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    motor_model_stop(&s_motor);
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
    ESP_LOGI(TAG, "Stop requested");
}

void motor_driver_mark_closed(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    motor_model_mark_closed(&s_motor);
    xSemaphoreGive(s_lock);
    motor_deenergize();
    motor_save_position(0, true);
    xTaskNotifyGive(s_task);
    if (s_callback != NULL) {
        s_callback(100, false);
    }
    ESP_LOGI(TAG, "Closed position calibrated");
}

bool motor_driver_is_calibrated(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool calibrated = motor_model_is_calibrated(&s_motor);
    xSemaphoreGive(s_lock);
    return calibrated;
}

uint8_t motor_driver_get_lift_percentage(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t percentage = motor_model_current_lift_percentage(&s_motor);
    xSemaphoreGive(s_lock);
    return percentage;
}

bool motor_driver_set_speed(uint16_t steps_per_second)
{
    if (steps_per_second < MOTOR_MIN_SPEED_STEPS_PER_SECOND ||
        steps_per_second > MOTOR_MAX_SPEED_STEPS_PER_SECOND) {
        ESP_LOGW(TAG, "Rejected speed: %u steps/s", steps_per_second);
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_speed_steps_per_second = steps_per_second;
    xSemaphoreGive(s_lock);
    if (s_nvs != 0) {
        nvs_set_u16(s_nvs, "speed", steps_per_second);
        nvs_commit(s_nvs);
    }
    ESP_LOGI(TAG, "Motor speed set to %u steps/s", steps_per_second);
    return true;
}

uint16_t motor_driver_get_speed(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t speed = s_speed_steps_per_second;
    xSemaphoreGive(s_lock);
    return speed;
}
