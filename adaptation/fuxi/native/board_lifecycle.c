/* SPDX-License-Identifier: BSD-2-Clause */
#include "board_lifecycle.h"
#define LOAD(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define STORE(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
static int prepare(struct st_board *b);

void st_board_init(struct st_board *b,const struct st_board_ops *ops) {
    if(b) {
        struct st_board blank={0}; *b=blank;
        if(ops)b->ops=*ops;
        fuxi_st_bridge_init(&b->bridge);
    }
}
int st_board_start(struct st_board *b) {
    if(fuxi_st_board_start()!=ST_OK)return ST_BLOCKED;
    return prepare(b);
}

static int teardown(struct st_board *b,int fault) {
    int rc,i;
    STORE(&b->accepting,0); b->state=ST_BOARD_QUIESCING;b->cleanup_error=0;
    if(b->lease[ST_IRQ]) {
        rc=b->ops.irq_mask(b->ops.ctx);
        if(rc)goto blocked;
        rc=b->ops.irq_quiesce(b->ops.ctx);
        if(rc)goto blocked;
    }
    STORE(&b->pending,0);
    if(b->lease[ST_HID] && !b->hid_released) {
        rc=fuxi_st_bridge_release(&b->bridge,b->ops.deliver,b->ops.hid);
        if(rc)goto blocked;
        b->hid_released=1;
    }
    for(i=ST_RESOURCE_COUNT-1;i>=0;i--)if(b->lease[i]) {
        rc=b->ops.release(b->ops.ctx,(enum st_resource)i,b->lease[i]);
        if(rc)goto blocked; /* preserve token AND all its prerequisites for retry */
        b->lease[i]=0;
    }
    b->state=fault?ST_BOARD_FAULT:ST_BOARD_STOPPED;
    return ST_OK;
blocked:
    b->cleanup_error=rc;b->state=ST_BOARD_FAULT;return rc;
}
static int fail(struct st_board *b,int error) {
    if(!b->last_error)b->last_error=error;
    (void)teardown(b,1);
    return error;
}
int st_board_stop(struct st_board *b) {
    unsigned i;int owned=0;
    if(!b)return ST_ARGUMENT;
    for(i=0;i<ST_RESOURCE_COUNT;i++)if(b->lease[i])owned=1;
    if(!owned) { STORE(&b->accepting,0);STORE(&b->pending,0);b->state=ST_BOARD_STOPPED;return ST_OK; }
    return teardown(b,0);
}
int st_board_suspend(struct st_board *b) {
    int rc;
    if(!b || b->state!=ST_BOARD_RUNNING)return ST_BLOCKED;
    rc=st_board_stop(b);
    if(!rc)b->state=ST_BOARD_SUSPENDED;
    return rc;
}
int st_board_resume(struct st_board *b) {
    if(!b || b->state!=ST_BOARD_SUSPENDED)return ST_BLOCKED;
    return st_board_start(b);
}
int st_board_irq(struct st_board *b) {
    int rc;
    if(!b || !LOAD(&b->accepting))return 0;
    rc=b->ops.irq_claim(b->ops.ctx);
    if(rc==0)return 0;
    if(rc>0)rc=b->ops.irq_mask(b->ops.ctx);
    if(rc) { STORE(&b->async_error,1);STORE(&b->accepting,0); }
    if(!__atomic_exchange_n(&b->pending,1,__ATOMIC_ACQ_REL))b->ops.wake_worker(b->ops.ctx);
    return 1;
}
int st_board_worker(struct st_board *b) {
    unsigned pass;int rc,active;
    uint8_t events[32][8];size_t count;
    if(!b || b->state!=ST_BOARD_RUNNING)return ST_BLOCKED;
    if(!__atomic_exchange_n(&b->pending,0,__ATOMIC_ACQ_REL))return ST_OK;
    if(LOAD(&b->async_error))return fail(b,ST_IO);
    /* At most 8 x 32 events per wake. Valid continuous traffic yields to the
     * workloop; repeated EMPTY reads with asserted IRQ quarantine the device. */
    for(pass=0;pass<8;pass++) {
        rc=fuxi_st_read_fifo(b->ops.exchange,b->ops.ctx,events,&count);
        if(rc)return fail(b,rc);
        rc=fuxi_st_bridge_events(&b->bridge,events,count,b->ops.deliver,b->ops.hid);
        if(rc)return fail(b,rc);
        active=b->ops.irq_active(b->ops.ctx);
        if(active<0)return fail(b,active);
        if(!active) {
            b->empty_rounds=0;
            rc=b->ops.irq_ack(b->ops.ctx);
            if(rc)return fail(b,rc);
            rc=b->ops.irq_unmask(b->ops.ctx);
            return rc?fail(b,rc):ST_OK;
        }
        if(count)b->empty_rounds=0;
        else if(++b->empty_rounds>=8)return fail(b,-110);
    }
    if(!__atomic_exchange_n(&b->pending,1,__ATOMIC_ACQ_REL))b->ops.wake_worker(b->ops.ctx);
    return ST_OK;
}
static int prepare(struct st_board *b) {
    unsigned i;int rc,active;
    if(!b)return ST_ARGUMENT;
    if(b->state!=ST_BOARD_NEW && b->state!=ST_BOARD_STOPPED && b->state!=ST_BOARD_SUSPENDED)return ST_BLOCKED;
    for(i=0;i<ST_RESOURCE_COUNT;i++)if(b->lease[i])return ST_BLOCKED;
    if(!b->ops.acquire || !b->ops.release || !b->ops.qualify_controller || !b->ops.irq_claim ||
       !b->ops.irq_mask || !b->ops.irq_quiesce || !b->ops.irq_ack || !b->ops.irq_unmask ||
       !b->ops.irq_active || !b->ops.wake_worker || !b->ops.exchange || !b->ops.deliver)return ST_ARGUMENT;
    b->state=ST_BOARD_PREPARING;b->last_error=0;b->cleanup_error=0;b->hid_released=0;
    b->empty_rounds=0;
    STORE(&b->pending,0);STORE(&b->async_error,0);fuxi_st_bridge_init(&b->bridge);
    for(i=0;i<ST_RESOURCE_COUNT;i++) {
        rc=b->ops.acquire(b->ops.ctx,(enum st_resource)i,&b->lease[i]);
        if(rc)return fail(b,rc);
        if(!b->lease[i])return fail(b,ST_PROTOCOL);
    }
    rc=b->ops.qualify_controller(b->ops.ctx);
    if(rc)return fail(b,rc);
    b->state=ST_BOARD_RUNNING;STORE(&b->accepting,1);
    active=b->ops.irq_active(b->ops.ctx);
    if(active<0)return fail(b,active);
    if(active) { STORE(&b->pending,1);b->ops.wake_worker(b->ops.ctx);return ST_OK; }
    rc=b->ops.irq_unmask(b->ops.ctx);
    return rc?fail(b,rc):ST_OK;
}
#ifdef ST_BOARD_HOST_TEST
int st_board_start_offline(struct st_board *b) { return prepare(b); }
#endif
