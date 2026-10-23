/* SPDX-License-Identifier: BSD-2-Clause
 * Original implementation from local-module protocol observations. No Linux
 * driver implementation copied. No allocation, syscalls, MMIO or firmware writes.
 */
#include "fuxi_st.h"

void fuxi_st_init(struct st_state *s) {
    if(s) { struct st_state empty={0}; *s=empty; }
}
int fuxi_st_board_start(void) { return ST_BLOCKED; }

int fuxi_st_read_fifo(st_exchange exchange, void *ctx, uint8_t events[32][8], size_t *count) {
    uint8_t rx[1+31*8]={0};
    uint8_t first[9]={0};
    size_t remaining,i,j;
    if(count) *count=0;
    if(!exchange || !events || !count) return ST_ARGUMENT;
    if(exchange(ctx,0x87,first,sizeof(first))) return ST_IO;
    if(first[1]==0) return ST_OK;
    remaining=first[8]&31u;
    if(remaining && exchange(ctx,0x87,rx,1+remaining*8)) return ST_IO;
    /* Do not publish a partial first event when the second transaction failed. */
    for(j=0;j<8;j++) events[0][j]=first[j+1];
    for(i=0;i<remaining;i++) for(j=0;j<8;j++) events[i+1][j]=rx[1+i*8+j];
    *count=remaining+1;
    return ST_OK;
}
static uint16_t normalize(uint16_t n, uint16_t max) {
    return (uint16_t)(((uint32_t)n*32767u+max/2u)/max);
}
static void report(const struct st_state *s, int lifted, struct st_reports *out) {
    uint8_t *r=out->bytes[out->count++];
    size_t i,j=0,k;
    for(k=0;k<62;k++) r[k]=0;
    r[0]=3;
    for(i=0;i<10;i++) if(s->contact[i].down || (int)i==lifted) {
        const struct st_contact *c=&s->contact[i];
        uint16_t x=normalize(c->x,10799),y=normalize(c->y,23999);
        uint8_t *row=r+1+6*j++;
        row[0]=(int)i==lifted?2:3; row[1]=(uint8_t)i;
        row[2]=(uint8_t)x;row[3]=(uint8_t)(x>>8);
        row[4]=(uint8_t)y;row[5]=(uint8_t)(y>>8);
    }
    r[61]=(uint8_t)j;
}
int fuxi_st_cancel(struct st_state *s, struct st_reports *out) {
    size_t i,j=0;
    if(!s || !out) return ST_ARGUMENT;
    out->count=0;
    /* One report of all old contacts with Tip=0, preserving their positions. */
    for(i=0;i<10;i++) if(s->contact[i].down) j++;
    if(j) {
        report(s,-1,out);
        for(i=0;i<j;i++) out->bytes[0][1+i*6]=2;
    }
    for(i=0;i<10;i++) s->contact[i].down=0;
    return ST_OK;
}
int fuxi_st_decode(struct st_state *s, const uint8_t events[][8], size_t count,
                   struct st_reports *out) {
    struct st_state next;
    size_t i;
    if(!s || !out || (!events && count) || count>32) return ST_ARGUMENT;
    out->count=0;
    if(s->reinit) return ST_REINIT;
    next=*s;
    for(i=0;i<count;i++) {
        const uint8_t *e=events[i];
        unsigned kind=e[0]>>4,id=e[1]>>4,type=e[1]&15;
        uint16_t x,y;
        if(e[0]==0) break;
        if(kind==0 || kind==15) {
            /* Discard any unsubmitted intermediate reports; the HID consumer
             * only knows the original state. Never auto-reinitialize hardware. */
            fuxi_st_cancel(s,out); s->reinit=1; return ST_REINIT;
        }
        if(kind<1 || kind>3) { out->count=0; return ST_UNSUPPORTED; }
        if(id>=10) { out->count=0; return ST_PROTOCOL; }
        if(type!=1) { out->count=0; return ST_UNSUPPORTED; }
        if(kind==3) {
            if(next.contact[id].down) {
                next.contact[id].down=0;
                report(&next,(int)id,out);
            }
            continue;
        }
        x=(uint16_t)(e[2]|((uint16_t)e[3]<<8));
        y=(uint16_t)(e[4]|((uint16_t)e[5]<<8));
        /* Reject invalid coordinates instead of hiding format/geometry errors. */
        if(x>10799 || y>23999) { out->count=0; return ST_PROTOCOL; }
        next.contact[id].x=x; next.contact[id].y=y; next.contact[id].down=1;
        report(&next,-1,out);
    }
    *s=next;
    return ST_OK;
}
