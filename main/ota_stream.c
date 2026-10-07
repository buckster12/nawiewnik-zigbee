#include "ota_stream.h"
#include <string.h>
static uint32_t le(const uint8_t *p, unsigned n) {
    uint32_t v=0; for (unsigned i=0;i<n;i++) v |= (uint32_t)p[i] << (8*i); return v;
}
void ota_stream_init(ota_stream_t *s, uint32_t total, uint32_t capacity, uint32_t version) {
    memset(s,0,sizeof(*s)); s->total=total; s->capacity=capacity; s->version=version;
}
int ota_stream_feed(ota_stream_t *s, uint32_t offset, const uint8_t *data, unsigned len, ota_sink_t sink, void *ctx) {
    if (s->failed || !data || !len || !sink || offset!=s->offset || offset>s->total || len>s->total-offset) goto fail;
    while (len && s->offset<62) { s->header[s->offset++]=*data++; len--; }
    if (s->offset==62 && !s->validated) {
        const uint8_t *h=s->header;
        s->payload=le(h+58,4);
        if (le(h,4)!=0x0beef11e || le(h+4,2)!=0x100 || le(h+6,2)!=56 ||
            le(h+8,2)!=0 || le(h+10,2)!=NAW_OTA_MANUFACTURER || le(h+12,2)!=NAW_OTA_IMAGE_TYPE ||
            le(h+14,4)!=s->version || le(h+18,2)!=2 || le(h+52,4)!=s->total || le(h+56,2)!=0 ||
            !s->payload || s->payload>s->capacity || s->total<62 || s->payload!=s->total-62) goto fail;
        s->validated=true;
    }
    if (len) { if (sink(data,len,ctx)) goto fail; s->offset+=len; }
    return 0;
fail: s->failed=true; return -1;
}
bool ota_stream_complete(const ota_stream_t *s) { return !s->failed && s->validated && s->offset==s->total; }
