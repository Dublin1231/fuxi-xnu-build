/* SPDX-License-Identifier: BSD-2-Clause */
#include "shared_board_backend.h"
static int acquire(void *ctx,enum st_resource r,uintptr_t *token) {
    struct st_shared_backend *b=ctx;struct ssb_bundle *bundle;unsigned ids[3],count=0,i;int rc=ST_OK;
    if(token)*token=0;
    if(!token || r<0 || r>=ST_RESOURCE_COUNT)return ST_ARGUMENT;
    if(!b->manager)return ST_BLOCKED;
    bundle=&b->bundle[r];if(bundle->token || b->generation>=UINTPTR_MAX/256)return ST_BLOCKED;
    switch(r) {
    case ST_VDD:ids[0]=SR_VDD;count=1;break;
    case ST_AVDD:ids[0]=SR_AVDD;count=1;break;
    case ST_CLOCK_M:ids[0]=SR_CLOCK_M;count=1;break;
    case ST_CLOCK_S:ids[0]=SR_CLOCK_S;count=1;break;
    case ST_CLOCK_SE:ids[0]=SR_CLOCK_SE;count=1;break;
    case ST_INTERCONNECT:ids[0]=SR_ICC_CORE;ids[1]=SR_ICC_CONFIG;ids[2]=SR_ICC_MEMORY;count=3;break;
    default:break;
    }
    bundle->shared=count!=0;bundle->count=count?count:1;
    if(count) {
        for(i=0;i<count;i++) {
            rc=sr_acquire(b->manager,ids[i],b->owner,&b->demand[ids[i]],&bundle->part[i]);
            if(rc)break;
        }
    } else {
        rc=b->platform.acquire?b->platform.acquire(b->platform.ctx,r,&bundle->part[0]):ST_BLOCKED;
    }
    for(i=0;i<bundle->count;i++)if(bundle->part[i]) {
        *token=bundle->token=(++b->generation)*256+(unsigned)r+1;return rc;
    }
    return rc?rc:ST_PROTOCOL;
}
static int release(void *ctx,enum st_resource r,uintptr_t token) {
    struct st_shared_backend *b=ctx;struct ssb_bundle *bundle;int rc;unsigned i;
    if(r<0 || r>=ST_RESOURCE_COUNT)return ST_ARGUMENT;
    bundle=&b->bundle[r];if(!token || bundle->token!=token)return ST_ARGUMENT;
    for(i=bundle->count;i>0;i--)if(bundle->part[i-1]) {
        rc=bundle->shared?sr_release(b->manager,b->owner,bundle->part[i-1]):
            (b->platform.release?b->platform.release(b->platform.ctx,r,bundle->part[i-1]):ST_BLOCKED);
        if(rc)return rc;
        bundle->part[i-1]=0;
    }
    bundle->token=0;return ST_OK;
}
#define FORWARD(name) static int name(void *ctx) {struct st_shared_backend *b=ctx;return b->manager && b->platform.name?b->platform.name(b->platform.ctx):ST_BLOCKED;}
FORWARD(qualify_controller)
FORWARD(irq_claim)
FORWARD(irq_mask)
FORWARD(irq_quiesce)
FORWARD(irq_ack)
FORWARD(irq_unmask)
FORWARD(irq_active)
static void wake_worker(void *ctx) {struct st_shared_backend *b=ctx;if(b->manager && b->platform.wake_worker)b->platform.wake_worker(b->platform.ctx);}
static int exchange(void *ctx,uint8_t command,uint8_t *rx,size_t len) {
    struct st_shared_backend *b=ctx;
    return b->manager && b->platform.exchange?b->platform.exchange(b->platform.ctx,command,rx,len):ST_BLOCKED;
}
int ssb_detach(struct st_shared_backend *b) {
    unsigned i;if(!b)return ST_ARGUMENT;
    for(i=0;i<ST_RESOURCE_COUNT;i++)if(b->bundle[i].token)return ST_BLOCKED;
    b->manager=0;b->owner=0;return ST_OK;
}
int ssb_init(struct st_shared_backend *b,struct sr_manager *manager,uint32_t owner,
             const struct st_board_ops *platform,const struct sr_request demand[SR_RESOURCES],struct st_board_ops *out) {
    unsigned i;
    if(!b || !manager || !owner || !platform || !demand || !out)return ST_ARGUMENT;
    if(b->manager)return ST_BLOCKED;
    for(i=0;i<ST_RESOURCE_COUNT;i++)if(b->bundle[i].token)return ST_BLOCKED;
    for(i=0;i<SR_RESOURCES;i++) {
        const struct sr_vote *v=&demand[i].active;
        if(v->enabled!=1)return ST_ARGUMENT;
        if(i<SR_ICC_CORE && (!v->minimum || v->minimum>v->maximum))return ST_ARGUMENT;
        if(i>=SR_ICC_CORE && !v->average && !v->peak)return ST_ARGUMENT;
    }
    { struct st_shared_backend blank={0};blank.generation=b->generation;*b=blank; }
    b->manager=manager;b->owner=owner;b->platform=*platform;
    for(i=0;i<SR_RESOURCES;i++)b->demand[i]=demand[i];
    *out=*platform;out->ctx=b;out->acquire=acquire;out->release=release;
    out->qualify_controller=qualify_controller;out->irq_claim=irq_claim;out->irq_mask=irq_mask;
    out->irq_quiesce=irq_quiesce;out->irq_ack=irq_ack;out->irq_unmask=irq_unmask;out->irq_active=irq_active;
    out->wake_worker=wake_worker;out->exchange=exchange;
    return ST_OK;
}
