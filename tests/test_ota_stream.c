#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ota_stream.h"
static unsigned written;
static int sink(const unsigned char *p, unsigned n, void *ctx) { (void)ctx; assert(p); written += n; return 0; }
int main(void) {
    unsigned char file[66] = {0x1e,0xf1,0xee,0x0b,0,1,56,0,0,0,0x34,0x12,1,0,2,0,0,0,2,0};
    file[52]=66; file[58]=4; file[62]=0xe9;
    ota_stream_t s;
    ota_stream_init(&s,66,4,2);
    for (unsigned i=0;i<66;i++) assert(ota_stream_feed(&s,i,file+i,1,sink,0)==0);
    assert(written==4); assert(ota_stream_complete(&s));
    /* Every header field is checked before any flash write. */
    const unsigned fields[]={0,4,6,8,10,12,14,18,52,56,58};
    for (unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        unsigned char bad[66]; memcpy(bad,file,66); bad[fields[i]]^=1;
        written=0; ota_stream_init(&s,66,4,2);
        assert(ota_stream_feed(&s,0,bad,66,sink,0)!=0);
        assert(!ota_stream_complete(&s)); assert(written==0);
    }
    written=0; ota_stream_init(&s,66,3,2);
    assert(ota_stream_feed(&s,0,file,66,sink,0)!=0); assert(written==0);
    ota_stream_init(&s,66,4,2);
    assert(ota_stream_feed(&s,1,file,1,sink,0)!=0);
    assert(ota_stream_feed(&s,0,file,66,sink,0)!=0);
    ota_stream_init(&s,66,4,2);
    assert(ota_stream_feed(&s,0,file,65,sink,0)==0); assert(!ota_stream_complete(&s));
    assert(ota_stream_feed(&s,65,file,2,sink,0)!=0);
    ota_stream_init(&s,66,4,2);
    assert(ota_stream_feed(&s,0,file,62,sink,0)==0);
    assert(ota_stream_feed(&s,0,file,62,sink,0)!=0);
    ota_stream_init(&s,66,4,2);
    assert(ota_stream_feed(&s,0,0,1,sink,0)!=0);
    puts("OTA fragmented container and rejection tests passed");
}
