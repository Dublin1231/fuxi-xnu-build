/* Synthetic shared provider; all quantities are test fixtures, not device data. */
#include "shared_board_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions,scenarios;
#define CHECK(x) do{assertions++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct fake {
    struct sr_manager manager;struct sr_request physical[SR_RESOURCES];
    unsigned qualified,calls,reads,fail_resource,mode,write_on_uncertain;
    unsigned platforms,released,delivered,active;
};
static struct sr_request range(uint64_t minimum,uint64_t maximum) {
    struct sr_request r={0};r.active.enabled=1;r.active.minimum=minimum;r.active.maximum=maximum;return r;
}
static struct sr_request bandwidth(uint64_t average,uint64_t peak) {
    struct sr_request r={0};r.active.enabled=1;r.active.average=average;r.active.peak=peak;return r;
}
static int qualified(void *ctx,unsigned r){(void)r;return (int)((struct fake *)ctx)->qualified;}
static int observe(void *ctx,unsigned r,struct sr_request *v){struct fake *f=ctx;f->reads++;*v=f->physical[r];return 0;}
static enum sr_commit apply(void *ctx,unsigned r,const struct sr_request *before,const struct sr_request *after) {
    struct fake *f=ctx;f->calls++;
    CHECK(before->active.enabled==f->physical[r].active.enabled);
    CHECK(before->active.minimum==f->physical[r].active.minimum);
    CHECK(before->active.average==f->physical[r].active.average);
    if(f->fail_resource==r+1) {
        unsigned mode=f->mode;f->fail_resource=0;
        if(mode==SR_INDETERMINATE && f->write_on_uncertain)f->physical[r]=*after;
        return (enum sr_commit)mode;
    }
    f->physical[r]=*after;return SR_APPLIED;
}
static void init(struct fake *f,int import) {
    memset(f,0,sizeof(*f));f->qualified=1;
    struct sr_provider p={f,qualified,observe,apply};sr_init(&f->manager,&p);
    if(import)for(unsigned i=0;i<SR_RESOURCES;i++)CHECK(sr_import_baseline(&f->manager,i)==0);
    scenarios++;
}
static unsigned leases(struct fake *f){unsigned n=0;for(unsigned i=0;i<SR_LEASES;i++)n+=f->manager.lease[i].state!=0;return n;}
static int p_acquire(void *ctx,enum st_resource r,uintptr_t *token) {
    struct fake *f=ctx;CHECK(r>=ST_PINCTRL);f->platforms++;*token=100u+(unsigned)r;return 0;
}
static int p_release(void *ctx,enum st_resource r,uintptr_t token) {
    struct fake *f=ctx;CHECK(token==100u+(unsigned)r);f->released++;return 0;
}
static int p_ok(void *ctx){(void)ctx;return 0;}
static int p_claim(void *ctx){(void)ctx;return 1;}
static int p_active(void *ctx){return (int)((struct fake *)ctx)->active;}
static void p_wake(void *ctx){(void)ctx;}
static int p_exchange(void *ctx,uint8_t cmd,uint8_t *rx,size_t n){(void)ctx;CHECK(cmd==0x87 && n==9);memset(rx,0,n);return 0;}
static int p_deliver(void *ctx,const uint8_t *r,size_t n){struct fake *f=ctx;CHECK(n==1 && r[0]==3 && r[61]==10);f->delivered++;return 0;}
static void bind(struct fake *f,struct st_shared_backend *b,struct st_board *board) {
    struct st_board_ops p={f,p_acquire,p_release,p_ok,p_claim,p_ok,p_ok,p_ok,p_ok,p_active,p_wake,p_exchange,p_deliver,f},out;
    struct sr_request demand[SR_RESOURCES];
    for(unsigned i=0;i<SR_RESOURCES;i++)demand[i]=i>=SR_ICC_CORE?bandwidth(100,200):range(100,1000);
    CHECK(ssb_init(b,&f->manager,7,&p,demand,&out)==0);st_board_init(board,&out);
}
int main(void) {
    struct fake f;uintptr_t t=0,u=0;struct sr_request a,b;
    init(&f,0);a=range(100,1000);CHECK(sr_acquire(&f.manager,SR_VDD,1,&a,&t)==ST_BLOCKED);CHECK(t==0 && f.calls==0);
    f.qualified=0;CHECK(sr_import_baseline(&f.manager,SR_VDD)==ST_BLOCKED);CHECK(f.reads==0);
    init(&f,1);a=range(100,1000);b=range(200,500);
    CHECK(sr_acquire(&f.manager,SR_VDD,1,&a,&t)==0);CHECK(sr_acquire(&f.manager,SR_VDD,2,&b,&u)==0);
    CHECK(f.physical[0].active.minimum==200 && f.physical[0].active.maximum==500);
    CHECK(sr_release(&f.manager,1,t)==0);CHECK(f.physical[0].active.enabled && f.physical[0].active.minimum==200);
    CHECK(sr_release(&f.manager,2,u)==0);CHECK(!f.physical[0].active.enabled && leases(&f)==0);
    init(&f,0);f.physical[0]=range(180,180);CHECK(sr_import_baseline(&f.manager,0)==0);a=range(180,500);
    CHECK(sr_acquire(&f.manager,0,1,&a,&t)==0);CHECK(sr_release(&f.manager,1,t)==0);CHECK(f.physical[0].active.enabled && f.physical[0].active.minimum==180);
    init(&f,1);a=range(100,100);CHECK(sr_acquire(&f.manager,0,1,&a,&t)==0);b=range(200,300);
    CHECK(sr_acquire(&f.manager,0,2,&b,&u)==ST_PROTOCOL);CHECK(u==0 && leases(&f)==1);
    CHECK(sr_release(&f.manager,2,t)==ST_ARGUMENT);CHECK(sr_release(&f.manager,1,t)==0);
    CHECK(sr_release(&f.manager,1,t)==ST_ARGUMENT);CHECK(sr_acquire(&f.manager,0,1,&a,&u)==0);CHECK(t!=u);
    CHECK(sr_release(&f.manager,1,t)==ST_ARGUMENT);CHECK(sr_release(&f.manager,1,u)==0);
    init(&f,1);a=bandwidth(100,200);b=bandwidth(150,170);
    CHECK(sr_acquire(&f.manager,SR_ICC_CORE,1,&a,&t)==0);CHECK(sr_acquire(&f.manager,SR_ICC_CORE,2,&b,&u)==0);
    CHECK(f.physical[SR_ICC_CORE].active.average==250 && f.physical[SR_ICC_CORE].active.peak==200);
    CHECK(sr_release(&f.manager,1,t)==0);CHECK(f.physical[SR_ICC_CORE].active.average==150);
    CHECK(sr_release(&f.manager,2,u)==0);
    init(&f,0);f.physical[SR_ICC_CORE]=bandwidth(UINT64_MAX,UINT64_MAX);CHECK(sr_import_baseline(&f.manager,SR_ICC_CORE)==0);
    a=bandwidth(1,2);CHECK(sr_acquire(&f.manager,SR_ICC_CORE,1,&a,&t)==ST_PROTOCOL);CHECK(t==0 && f.calls==0);
    init(&f,1);a=range(10,100);a.sleep.enabled=1;a.sleep.minimum=5;a.sleep.maximum=50;
    CHECK(sr_acquire(&f.manager,0,1,&a,&t)==0);CHECK(f.physical[0].sleep.minimum==5);CHECK(sr_release(&f.manager,1,t)==0);
    for(unsigned removal=0;removal<2;removal++)for(unsigned observed_new=0;observed_new<2;observed_new++) {
        init(&f,1);a=range(100,1000);
        if(removal)CHECK(sr_acquire(&f.manager,0,1,&a,&t)==0);
        f.fail_resource=1;f.mode=SR_INDETERMINATE;f.write_on_uncertain=observed_new;
        CHECK((removal?sr_release(&f.manager,1,t):sr_acquire(&f.manager,0,1,&a,&t))==ST_IO);
        CHECK(t!=0 && f.manager.resource[0].frozen && leases(&f)==1);
        CHECK(sr_release(&f.manager,1,t)==ST_BLOCKED);
        CHECK(sr_acquire(&f.manager,0,2,&a,&u)==ST_BLOCKED);
        CHECK(sr_reconcile(&f.manager,0)==0);CHECK(!f.manager.resource[0].frozen);
        CHECK(sr_release(&f.manager,1,t)==0);CHECK(leases(&f)==0 && !f.physical[0].active.enabled);
    }
    init(&f,1);a=range(100,1000);f.fail_resource=1;f.mode=SR_UNCHANGED;
    CHECK(sr_acquire(&f.manager,0,1,&a,&t)==ST_IO);CHECK(t==0 && leases(&f)==0 && !f.manager.resource[0].frozen);
    CHECK(sr_acquire(&f.manager,0,1,&a,&t)==0);f.fail_resource=1;
    CHECK(sr_release(&f.manager,1,t)==ST_IO);CHECK(leases(&f)==1);CHECK(sr_release(&f.manager,1,t)==0);
    init(&f,1);a=range(100,1000);f.fail_resource=1;f.mode=SR_INDETERMINATE;
    CHECK(sr_acquire(&f.manager,0,1,&a,&t)==ST_IO);f.physical[0]=range(777,888);
    CHECK(sr_reconcile(&f.manager,0)==ST_BLOCKED);CHECK(f.manager.resource[0].frozen);
    f.physical[0]=(struct sr_request){0};CHECK(sr_reconcile(&f.manager,0)==0);CHECK(sr_release(&f.manager,1,t)==0);
    init(&f,1);a=range(100,1000);CHECK(sr_acquire(&f.manager,0,1,&a,&t)==0);f.qualified=0;
    CHECK(sr_acquire(&f.manager,0,2,&a,&u)==ST_BLOCKED);CHECK(sr_release(&f.manager,1,t)==ST_BLOCKED);
    f.qualified=1;CHECK(sr_release(&f.manager,1,t)==0);
    init(&f,1);a=range(100,1000);uintptr_t tokens[SR_LEASES];
    for(unsigned i=0;i<SR_LEASES;i++)CHECK(sr_acquire(&f.manager,0,1,&a,&tokens[i])==0);
    CHECK(sr_acquire(&f.manager,0,1,&a,&t)==ST_BLOCKED);
    for(unsigned i=0;i<SR_LEASES;i++)CHECK(sr_release(&f.manager,1,tokens[i])==0);
    f.manager.generation=UINTPTR_MAX/256;CHECK(sr_acquire(&f.manager,0,1,&a,&t)==ST_BLOCKED);
    for(unsigned fail=0;fail<3;fail++) {
        struct st_shared_backend backend={0};struct st_board board;
        init(&f,1);bind(&f,&backend,&board);f.fail_resource=SR_ICC_CORE+fail+1;f.mode=SR_UNCHANGED;
        CHECK(st_board_start_offline(&board)==ST_IO);CHECK(leases(&f)==0 && f.platforms==0);
        for(unsigned i=0;i<ST_RESOURCE_COUNT;i++)CHECK(board.lease[i]==0);
    }
    {
        struct st_shared_backend backend={0};struct st_board board;
        init(&f,0);bind(&f,&backend,&board);CHECK(st_board_start_offline(&board)==ST_BLOCKED);CHECK(f.platforms==0 && leases(&f)==0);
        CHECK(ssb_detach(&backend)==0);
        init(&f,1);bind(&f,&backend,&board);a=range(50,1000);CHECK(sr_acquire(&f.manager,0,99,&a,&u)==0);
        CHECK(st_board_start(&board)==ST_BLOCKED);CHECK(f.platforms==0);
        CHECK(st_board_start_offline(&board)==0);CHECK(leases(&f)==9 && f.platforms==4);
        CHECK(ssb_detach(&backend)==ST_BLOCKED);
        uintptr_t old_bundle=board.lease[ST_VDD];
        CHECK(st_board_stop(&board)==0);CHECK(leases(&f)==1 && f.physical[0].active.enabled);CHECK(f.released==4 && f.delivered==1);
        CHECK(sr_release(&f.manager,99,u)==0);CHECK(leases(&f)==0);
        CHECK(ssb_detach(&backend)==0);bind(&f,&backend,&board);
        CHECK(st_board_start_offline(&board)==0);CHECK(board.lease[ST_VDD]!=old_bundle);
        CHECK(board.ops.release(board.ops.ctx,ST_VDD,old_bundle)==ST_ARGUMENT);
        CHECK(st_board_stop(&board)==0);CHECK(ssb_detach(&backend)==0);
    }
    {
        struct st_shared_backend backend={0};struct st_board board;
        init(&f,1);bind(&f,&backend,&board);CHECK(st_board_start_offline(&board)==0);
        f.fail_resource=SR_ICC_MEMORY+1;f.mode=SR_INDETERMINATE;f.write_on_uncertain=1;
        CHECK(st_board_stop(&board)==ST_IO);CHECK(board.lease[ST_INTERCONNECT]!=0 && board.lease[ST_VDD]!=0);
        CHECK(sr_reconcile(&f.manager,SR_ICC_MEMORY)==0);CHECK(st_board_stop(&board)==0);CHECK(leases(&f)==0 && f.released==4);
    }
    printf("{\"scenarios\":%u,\"assertions\":%u,\"hardware_tested\":false}\n",scenarios,assertions);
    return 0;
}
