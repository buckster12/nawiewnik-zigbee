#pragma once
#include <stdbool.h>
#include <stdint.h>
#define NAW_OTA_MANUFACTURER 0x1234
#define NAW_OTA_IMAGE_TYPE 1
#ifndef NAW_OTA_VERSION
#define NAW_OTA_VERSION 1
#endif
typedef struct {
    uint8_t header[62];
    uint32_t offset, total, capacity, version, payload;
    bool failed, validated;
} ota_stream_t;
typedef int (*ota_sink_t)(const uint8_t *, unsigned, void *);
void ota_stream_init(ota_stream_t *s, uint32_t total, uint32_t capacity, uint32_t version);
int ota_stream_feed(ota_stream_t *s, uint32_t offset, const uint8_t *data, unsigned len, ota_sink_t sink, void *ctx);
bool ota_stream_complete(const ota_stream_t *s);
