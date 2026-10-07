idf_component_register(
    SRCS
        "main.c"
        "ota_stream.c"
        "zigbee_ota.c"
        "motor_model.c"
        "homing_controller.c"
        "motor_driver.c"
        "status_led.c"
        "battery_monitor.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash esp_driver_gpio esp_timer esp_adc app_update
)
