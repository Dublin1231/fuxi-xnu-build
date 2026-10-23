/* SPDX-License-Identifier: BSD-2-Clause */
#include "fuxi_st_bridge.h"
void fuxi_st_bridge_init(struct st_bridge *b) {
    if(b) { fuxi_st_init(&b->state); b->faulted=0; }
}
int fuxi_st_bridge_release(struct st_bridge *b, st_deliver deliver, void *device) {
    uint8_t r[62]={3}; size_t i;
    if(!b || !deliver) return ST_ARGUMENT;
    b->faulted=1;
    for(i=0;i<10;i++) { r[1+6*i]=2; r[2+6*i]=(uint8_t)i; }
    r[61]=10;
    if(deliver(device,r,1)) return ST_IO;
    fuxi_st_init(&b->state);
    return ST_OK;
}
int fuxi_st_bridge_events(struct st_bridge *b, const uint8_t events[][8], size_t count,
                          st_deliver deliver, void *device) {
    struct st_reports out;
    int rc;
    if(!b || !deliver) return ST_ARGUMENT;
    if(b->faulted) return ST_BLOCKED;
    rc=fuxi_st_decode(&b->state,events,count,&out);
    if(rc!=ST_OK && rc!=ST_REINIT) { b->faulted=1; return rc; }
    if(rc==ST_REINIT) b->faulted=1;
    if(out.count && deliver(device,&out.bytes[0][0],out.count)) {
        b->faulted=1; return ST_IO;
    }
    return rc;
}
int fuxi_st_bridge_poll(struct st_bridge *b, st_exchange exchange, void *bus,
                        st_deliver deliver, void *device) {
    uint8_t events[32][8]; size_t count=0; int rc;
    if(!b || !exchange || !deliver) return ST_ARGUMENT;
    if(b->faulted) return ST_BLOCKED;
    rc=fuxi_st_read_fifo(exchange,bus,events,&count);
    if(rc) { b->faulted=1; return rc; }
    return fuxi_st_bridge_events(b,events,count,deliver,device);
}
