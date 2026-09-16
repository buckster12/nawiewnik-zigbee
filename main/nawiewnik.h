#pragma once

#include "esp_zigbee.h"
#include "ezbee/zha.h"

#define NAWIEWNIK_ENDPOINT 10
#define NAWIEWNIK_CHANNEL_MASK 0x07FFF800UL
#define NAWIEWNIK_ZB_STORAGE_PARTITION "nvs"
#define NAWIEWNIK_MANUFACTURER "\x0A" "SantaRumor"
#define NAWIEWNIK_MODEL "\x0C" "Nawiewnik-H2"

#define NAWIEWNIK_ZED_CONFIG()                       \
    {                                                \
        .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE, \
        .install_code_policy = false,                \
        .zed_config = {                              \
            .ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN,  \
            .keep_alive = 4000,                      \
        },                                           \
    }

#define NAWIEWNIK_PLATFORM_CONFIG()                              \
    {                                                            \
        .storage_partition_name = NAWIEWNIK_ZB_STORAGE_PARTITION, \
        .radio_config = {                                        \
            .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,          \
        },                                                       \
    }

#define NAWIEWNIK_ZIGBEE_CONFIG()                 \
    {                                             \
        .device_config = NAWIEWNIK_ZED_CONFIG(),  \
        .platform_config = NAWIEWNIK_PLATFORM_CONFIG(), \
    }
