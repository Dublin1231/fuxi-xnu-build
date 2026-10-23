/* Synthetic callback backend only. No MMIO or phone I/O. */
#include "board_lifecycle.h"
#include "kalama_registers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions,scenarios;
#define CHECK(x) do { assertions++; if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
struct fake {
    struct st_board board;
    int acquire_fail,release_fail,qualify_fail,mask_fail,quiesce_fail,ack_fail,unmask_fail,bus_fail,sink_fail;
    unsigned acquired,released,drains,delivered,wakes,masks,unmasks,acks,quiesces,claimed;
    int pin_low,not_ours,always_low,inject_irq,empty_fifo;
    unsigned refs[ST_RESOURCE_COUNT],order[ST_RESOURCE_COUNT],release_count;
};
static int acquire(void *ctx,enum st_resource r,uintptr_t *token) {
    struct fake *f=ctx;f->refs[r]++;f->acquired|=1u<<r;*token=0x100u+(unsigned)r;
    return f->acquire_fail==(int)r+1?-71:0;
}
static int release(void *ctx,enum st_resource r,uintptr_t token) {
    struct fake *f=ctx;CHECK(token==0x100u+(unsigned)r);CHECK(f->refs[r]==3);
    if(f->release_fail==(int)r+1){f->release_fail=0;return -5;}
    CHECK(f->release_count<ST_RESOURCE_COUNT);f->order[f->release_count++]=(unsigned)r;
    f->refs[r]--;f->released|=1u<<r;return 0;
}
static int qualify(void *ctx) {return ((struct fake *)ctx)->qualify_fail?-71:0;}
static int claim(void *ctx) {struct fake *f=ctx;f->claimed++;return !f->not_ours;}
static int mask(void *ctx) {struct fake *f=ctx;f->masks++;if(f->mask_fail){f->mask_fail=0;return -5;}return 0;}
static int quiesce(void *ctx) {struct fake *f=ctx;f->quiesces++;if(f->quiesce_fail){f->quiesce_fail=0;return -5;}return 0;}
static int ack(void *ctx) {struct fake *f=ctx;f->acks++;return f->ack_fail?-5:0;}
static int unmask(void *ctx) {struct fake *f=ctx;f->unmasks++;return f->unmask_fail?-5:0;}
static int active(void *ctx) {return ((struct fake *)ctx)->pin_low;}
static void wake(void *ctx) {((struct fake *)ctx)->wakes++;}
static int exchange(void *ctx,uint8_t cmd,uint8_t *rx,size_t n) {
    struct fake *f=ctx;f->drains++;CHECK(cmd==0x87);CHECK(n==9);
    CHECK(f->masks>0 || f->pin_low); /* top half or masked acquisition/start-low */
    if(f->bus_fail)return -5;
    memset(rx,0,n);rx[0]=0xa5;rx[1]=0x13;rx[2]=1;rx[3]=0x2f;rx[4]=0x2a;rx[5]=0xbf;rx[6]=0x5d;
    if(f->empty_fifo)rx[1]=0;
    if(f->inject_irq){f->inject_irq=0;CHECK(st_board_irq(&f->board)==1);}
    if(!f->always_low)f->pin_low=0;
    return 0;
}
static int deliver(void *ctx,const uint8_t *r,size_t n) {
    struct fake *f=ctx;f->delivered++;CHECK(n==1);CHECK(r[0]==3);
    if(r[61]==10)for(unsigned i=0;i<10;i++){CHECK(r[1+6*i]==2);CHECK(r[2+6*i]==i);}
    if(f->sink_fail){f->sink_fail--;return -5;}return 0;
}
static void init(struct fake *f) {
    memset(f,0,sizeof(*f));for(unsigned i=0;i<ST_RESOURCE_COUNT;i++)f->refs[i]=2;
    struct st_board_ops ops={f,acquire,release,qualify,claim,mask,quiesce,ack,unmask,active,wake,exchange,deliver,f};
    st_board_init(&f->board,&ops);scenarios++;
}
static void all_released(struct fake *f) {
    CHECK(f->acquired==f->released);
    for(unsigned i=0;i<ST_RESOURCE_COUNT;i++){CHECK(f->refs[i]==2);CHECK(f->board.lease[i]==0);}
    CHECK(f->board.accepting==0);
}
int main(void) {
    struct fake f;
    init(&f);CHECK(st_board_start(&f.board)==ST_BLOCKED);CHECK(f.acquired==0);CHECK(f.wakes==0);
    init(&f);f.board.ops.irq_quiesce=0;CHECK(st_board_start_offline(&f.board)==ST_ARGUMENT);CHECK(f.acquired==0);
    for(unsigned i=0;i<ST_RESOURCE_COUNT;i++) {
        init(&f);f.acquire_fail=(int)i+1;CHECK(st_board_start_offline(&f.board)==-71);all_released(&f);
        CHECK(f.board.state==ST_BOARD_FAULT);CHECK(f.release_count==i+1);
        for(unsigned j=0;j<f.release_count;j++)CHECK(f.order[j]==i-j);
    }
    init(&f);f.qualify_fail=1;CHECK(st_board_start_offline(&f.board)==-71);all_released(&f);CHECK(f.drains==0);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);CHECK(f.unmasks==1);
    CHECK(st_board_stop(&f.board)==0);all_released(&f);CHECK(f.delivered==1);CHECK(f.quiesces==1);
    CHECK(st_board_stop(&f.board)==0);CHECK(f.delivered==1);CHECK(f.release_count==ST_RESOURCE_COUNT);
    init(&f);f.pin_low=1;CHECK(st_board_start_offline(&f.board)==0);CHECK(f.wakes==1);CHECK(f.unmasks==0);
    CHECK(st_board_worker(&f.board)==0);CHECK(f.drains==1);CHECK(f.acks==1);CHECK(f.unmasks==1);CHECK(st_board_stop(&f.board)==0);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.not_ours=1;
    CHECK(st_board_irq(&f.board)==0);CHECK(f.masks==0);CHECK(f.wakes==0);CHECK(st_board_stop(&f.board)==0);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.pin_low=1;
    CHECK(st_board_irq(&f.board)==1);CHECK(st_board_irq(&f.board)==1);
    CHECK(f.drains==0);CHECK(f.delivered==0);CHECK(f.wakes==1);
    CHECK(st_board_worker(&f.board)==0);CHECK(f.drains==1);CHECK(st_board_stop(&f.board)==0);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.mask_fail=1;
    CHECK(st_board_irq(&f.board)==1);CHECK(f.released==0);CHECK(f.drains==0);
    CHECK(st_board_worker(&f.board)==ST_IO);all_released(&f);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.pin_low=1;f.bus_fail=1;
    CHECK(st_board_irq(&f.board)==1);CHECK(st_board_worker(&f.board)==ST_IO);all_released(&f);CHECK(f.drains==1);
    CHECK(st_board_worker(&f.board)==ST_BLOCKED);CHECK(f.drains==1);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.pin_low=1;f.sink_fail=1;
    CHECK(st_board_irq(&f.board)==1);CHECK(st_board_worker(&f.board)==ST_IO);all_released(&f);CHECK(f.delivered==2);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.pin_low=1;f.ack_fail=1;
    CHECK(st_board_irq(&f.board)==1);CHECK(st_board_worker(&f.board)==-5);all_released(&f);
    init(&f);f.unmask_fail=1;CHECK(st_board_start_offline(&f.board)==-5);all_released(&f);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.pin_low=1;f.always_low=1;f.empty_fifo=1;
    CHECK(st_board_irq(&f.board)==1);CHECK(st_board_worker(&f.board)==-110);CHECK(f.drains==8);all_released(&f);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.pin_low=1;f.always_low=1;
    CHECK(st_board_irq(&f.board)==1);CHECK(st_board_worker(&f.board)==0);CHECK(f.drains==8);
    CHECK(f.board.state==ST_BOARD_RUNNING && f.board.pending==1 && f.wakes==2);CHECK(f.acks==0);
    f.always_low=0;CHECK(st_board_worker(&f.board)==0);CHECK(f.drains==9);CHECK(f.acks==1);
    CHECK(st_board_stop(&f.board)==0);all_released(&f);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);CHECK(st_board_irq(&f.board)==1);
    CHECK(st_board_stop(&f.board)==0);CHECK(st_board_worker(&f.board)==ST_BLOCKED);CHECK(f.drains==0);all_released(&f);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.quiesce_fail=1;
    CHECK(st_board_stop(&f.board)==-5);CHECK(f.released==0);CHECK(f.delivered==0);
    CHECK(f.board.cleanup_error==-5);CHECK(st_board_stop(&f.board)==0);all_released(&f);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.sink_fail=1;
    CHECK(st_board_stop(&f.board)==ST_IO);CHECK(f.released==0);
    CHECK(st_board_stop(&f.board)==0);all_released(&f);
    for(unsigned i=0;i<ST_RESOURCE_COUNT;i++) {
        init(&f);CHECK(st_board_start_offline(&f.board)==0);f.release_fail=(int)i+1;
        CHECK(st_board_stop(&f.board)==-5);CHECK(f.board.lease[i]!=0);CHECK(f.refs[i]==3);
        for(unsigned j=0;j<=i;j++)CHECK(f.board.lease[j]!=0);
        CHECK(st_board_start_offline(&f.board)==ST_BLOCKED);
        CHECK(st_board_stop(&f.board)==0);all_released(&f);CHECK(f.delivered==1);
    }
    init(&f);CHECK(st_board_start_offline(&f.board)==0);f.pin_low=1;f.inject_irq=1;
    CHECK(st_board_irq(&f.board)==1);CHECK(st_board_worker(&f.board)==0);CHECK(f.wakes==2);
    CHECK(st_board_worker(&f.board)==0);CHECK(st_board_stop(&f.board)==0);all_released(&f);
    init(&f);CHECK(st_board_start_offline(&f.board)==0);CHECK(st_board_suspend(&f.board)==0);
    CHECK(f.board.state==ST_BOARD_SUSPENDED);all_released(&f);
    CHECK(st_board_resume(&f.board)==ST_BLOCKED);CHECK(f.release_count==ST_RESOURCE_COUNT);
    f.release_count=0;f.acquired=0;f.released=0;
    CHECK(st_board_start_offline(&f.board)==0);CHECK(st_board_stop(&f.board)==0);all_released(&f);
    {
        unsigned d=0;uint64_t hz=0;
        CHECK(st_gpio_ctl_address(25)==0x0f119000u);CHECK(st_gpio_ctl_address(44)==0x0f12c000u);
        CHECK(st_gpio_ctl_address(211)==0);
        CHECK(st_clock_divider(48000000,1,12000000,&d,&hz)==0);CHECK(d==4 && hz==12000000);
        CHECK(st_clock_divider(19200000,1,12000000,&d,&hz)==0);CHECK(d==2 && hz==9600000);
        CHECK(st_clock_divider(48000000,1,19200000,&d,&hz)!=0);
        CHECK(st_clock_divider(0,1,12000000,&d,&hz)!=0);
        CHECK(st_transport_policy(1,1,ST_TRANSPORT_SE_DMA,1,1)!=0);
        CHECK(st_transport_policy(1,0,ST_TRANSPORT_GPI,1,1)!=0);
        CHECK(st_transport_policy(1,0,ST_TRANSPORT_SE_DMA,1,0)!=0);
        CHECK(st_transport_policy(1,0,ST_TRANSPORT_PIO,1,0)==0);
    }
    printf("{\"scenarios\":%u,\"assertions\":%u,\"hardware_tested\":false}\n",scenarios,assertions);
    return 0;
}
