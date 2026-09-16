#include "status_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"

#define STATUS_LED_GPIO 8
#define STATUS_LED_BRIGHTNESS 12

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_led_lock;

static void set_white(uint8_t level)
{
    if (s_strip == NULL) {
        return;
    }
    led_strip_set_pixel(s_strip, 0, level, level, level);
    led_strip_refresh(s_strip);
}

static void clear_led(void)
{
    if (s_strip == NULL) {
        return;
    }
    led_strip_clear(s_strip);
}

esp_err_t status_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        return err;
    }
    s_led_lock = xSemaphoreCreateMutex();
    if (s_led_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    clear_led();
    return ESP_OK;
}

void status_led_confirm_calibration(void)
{
    if (s_led_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_led_lock, portMAX_DELAY);
    for (int i = 0; i < 3; ++i) {
        set_white(32);
        vTaskDelay(pdMS_TO_TICKS(150));
        clear_led();
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    xSemaphoreGive(s_led_lock);
}
