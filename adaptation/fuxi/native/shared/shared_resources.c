/* SPDX-License-Identifier: BSD-2-Clause */
#include "shared_resources.h"
enum { FREE,ACTIVE,PENDING_ACQUIRE,RELEASED_ACK,CANCELLED_ACK };
static int valid_vote(unsigned r,const struct sr_vote *v) {
    if(v->enabled>1)return 0;
    if(!v->enabled)return !v->minimum && !v->maximum && !v->average && !v->peak;
    if(r>=SR_ICC_CORE)return !v->minimum && !v->maximum;
    return v->minimum<=v->maximum && !v->average && !v->peak;
}
static int valid(unsigned r,const struct sr_request *v) {
    return v && valid_vote(r,&v->active) && valid_vote(r,&v->sleep);
}
static int eq_vote(const struct sr_vote *a,const struct sr_vote *b) {
    return a->enabled==b->enabled && a->minimum==b->minimum && a->maximum==b->maximum && a->average==b->average && a->peak==b->peak;
}
static int equal(const struct sr_request *a,const struct sr_request *b) {
    return eq_vote(&a->active,&b->active) && eq_vote(&a->sleep,&b->sleep);
}
static int merge(unsigned r,struct sr_vote *a,const struct sr_vote *b) {
    if(!b->enabled)return ST_OK;
    if(!a->enabled){*a=*b;return ST_OK;}
    if(r>=SR_ICC_CORE) {
        if(UINT64_MAX-a->average<b->average)return ST_PROTOCOL;
        a->average+=b->average;if(b->peak>a->peak)a->peak=b->peak;
    } else {
        if(b->minimum>a->minimum)a->minimum=b->minimum;
        if(b->maximum<a->maximum)a->maximum=b->maximum;
        if(a->minimum>a->maximum)return ST_PROTOCOL;
    }
    return ST_OK;
}
static int aggregate(struct sr_manager *m,unsigned r,int extra,int skip,struct sr_request *out) {
    unsigned i;int rc;*out=m->resource[r].baseline;
    for(i=0;i<SR_LEASES;i++)if((int)i!=skip && m->lease[i].resource==r &&
        (m->lease[i].state==ACTIVE || (int)i==extra)) {
        rc=merge(r,&out->active,&m->lease[i].request.active);if(rc)return rc;
        rc=merge(r,&out->sleep,&m->lease[i].request.sleep);if(rc)return rc;
    }
    return ST_OK;
}
void sr_init(struct sr_manager *m,const struct sr_provider *provider) {
    if(m){struct sr_manager blank={0};*m=blank;if(provider)m->provider=*provider;}
}
int sr_import_baseline(struct sr_manager *m,unsigned r) {
    struct sr_request observed={0};unsigned i;
    if(!m || r>=SR_RESOURCES)return ST_ARGUMENT;
    if(m->resource[r].frozen)return ST_BLOCKED;
    for(i=0;i<SR_LEASES;i++)if(m->lease[i].state!=FREE && m->lease[i].resource==r)return ST_BLOCKED;
    if(!m->provider.qualified || !m->provider.observe || !m->provider.apply ||
       m->provider.qualified(m->provider.ctx,r)!=1)return ST_BLOCKED;
    if(m->provider.observe(m->provider.ctx,r,&observed) || !valid(r,&observed))return ST_IO;
    m->resource[r].baseline=observed;m->resource[r].current=observed;m->resource[r].known=1;
    return ST_OK;
}
static void uncertain(struct sr_entry *e,unsigned slot,unsigned removal,const struct sr_request *desired) {
    e->frozen=1;e->pending_slot=slot;e->pending_remove=removal;e->desired=*desired;
}
int sr_acquire(struct sr_manager *m,unsigned r,uint32_t owner,const struct sr_request *request,uintptr_t *token) {
    unsigned i;int rc;struct sr_request desired;enum sr_commit result=SR_APPLIED;struct sr_entry *e;
    if(token)*token=0;
    if(!m || !token || r>=SR_RESOURCES || !owner || !valid(r,request))return ST_ARGUMENT;
    e=&m->resource[r];if(!e->known || e->frozen || !m->provider.qualified || m->provider.qualified(m->provider.ctx,r)!=1)return ST_BLOCKED;
    for(i=0;i<SR_LEASES;i++)if(m->lease[i].state==FREE)break;
    if(i==SR_LEASES || m->generation>=UINTPTR_MAX/256)return ST_BLOCKED;
    m->lease[i]=(struct sr_lease){*request,(++m->generation)*256+i+1,owner,r,PENDING_ACQUIRE};
    rc=aggregate(m,r,(int)i,-1,&desired);
    if(rc){m->lease[i].state=FREE;return rc;}
    if(!equal(&e->current,&desired))result=m->provider.apply(m->provider.ctx,r,&e->current,&desired);
    if(result==SR_UNCHANGED){m->lease[i].state=FREE;return ST_IO;}
    *token=m->lease[i].token;
    if(result!=SR_APPLIED){uncertain(e,i,0,&desired);return ST_IO;}
    e->current=desired;m->lease[i].state=ACTIVE;return ST_OK;
}
int sr_release(struct sr_manager *m,uint32_t owner,uintptr_t token) {
    unsigned slot=(unsigned)(token&255u);struct sr_lease *l;struct sr_entry *e;
    struct sr_request desired;enum sr_commit result=SR_APPLIED;int rc;
    if(!m || !owner || !slot || slot>SR_LEASES)return ST_ARGUMENT;
    l=&m->lease[slot-1];if(l->state==FREE || l->token!=token || l->owner!=owner)return ST_ARGUMENT;
    if(l->state==CANCELLED_ACK || l->state==RELEASED_ACK){l->state=FREE;return ST_OK;}
    e=&m->resource[l->resource];if(e->frozen)return ST_BLOCKED;
    if(!m->provider.qualified || m->provider.qualified(m->provider.ctx,l->resource)!=1)return ST_BLOCKED;
    if(l->state!=ACTIVE)return ST_BLOCKED;
    rc=aggregate(m,l->resource,-1,(int)slot-1,&desired);if(rc)return rc;
    if(!equal(&e->current,&desired))result=m->provider.apply(m->provider.ctx,l->resource,&e->current,&desired);
    if(result==SR_UNCHANGED)return ST_IO;
    if(result!=SR_APPLIED){uncertain(e,slot-1,1,&desired);return ST_IO;}
    e->current=desired;l->state=FREE;return ST_OK;
}
int sr_reconcile(struct sr_manager *m,unsigned r) {
    struct sr_entry *e;struct sr_lease *l;struct sr_request observed={0};
    if(!m || r>=SR_RESOURCES)return ST_ARGUMENT;
    e=&m->resource[r];if(!e->frozen)return ST_OK;
    if(!m->provider.qualified || m->provider.qualified(m->provider.ctx,r)!=1 ||
       m->provider.observe(m->provider.ctx,r,&observed))return ST_BLOCKED;
    l=&m->lease[e->pending_slot];
    if(equal(&observed,&e->desired)) {
        e->current=e->desired;l->state=e->pending_remove?RELEASED_ACK:ACTIVE;
    } else if(equal(&observed,&e->current)) {
        l->state=e->pending_remove?ACTIVE:CANCELLED_ACK;
    } else return ST_BLOCKED;
    e->frozen=0;return ST_OK;
}
static int unavailable(void *ctx,unsigned r){(void)ctx;(void)r;return 0;}
void sr_unavailable_provider(struct sr_provider *provider) {
    if(provider){struct sr_provider blank={0};*provider=blank;provider->qualified=unavailable;}
}
