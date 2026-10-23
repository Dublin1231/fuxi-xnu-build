/* SPDX-License-Identifier: BSD-2-Clause
 * Real IOKit API adapter SOURCE. No SDK compile/link/runtime claim.
 * No provider discovery, auto-registration, MMIO writes or default hardware vote.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOReturn.h>
#include "FuxiSharedRegistry.h"
enum RegistryOp { Import,Acquire,Release,Reconcile,Bind,Unbind,Close };
struct RegistryCall {
    RegistryOp op;unsigned resource;uint32_t owner;uintptr_t token;
    const sr_request *request;uintptr_t *resultToken;
    st_shared_backend *backend;const st_board_ops *platform;st_board_ops *resultOps;IOService *hid;
    int result;bool dropPin;
};
class FuxiSharedRegistry : public OSObject {
    OSDeclareDefaultStructors(FuxiSharedRegistry)
    IOWorkLoop *loop=nullptr;
    IOService *heldOwner=nullptr;
    OSObject *heldProvider=nullptr;
    sr_manager manager={};
    st_shared_backend *bindings[8]={};
    unsigned closed=0;bool pinned=false;
    static IOReturn action(OSObject *owner,void *arg0,void *,void *,void *) {
        FuxiSharedRegistry *self=static_cast<FuxiSharedRegistry *>(owner);
        RegistryCall *c=static_cast<RegistryCall *>(arg0);
        c->result=ST_BLOCKED;
        if(__atomic_load_n(&self->closed,__ATOMIC_ACQUIRE)){if(c->op==Close)c->result=ST_OK;return kIOReturnSuccess;}
        switch(c->op) {
        case Import:c->result=sr_import_baseline(&self->manager,c->resource);break;
        case Acquire:c->result=sr_acquire(&self->manager,c->resource,c->owner,c->request,c->resultToken);break;
        case Release:c->result=sr_release(&self->manager,c->owner,c->token);break;
        case Reconcile:c->result=sr_reconcile(&self->manager,c->resource);break;
        case Bind: {
            if(!c->backend || !c->platform || !c->hid || c->platform->hid!=c->hid || c->hid->getWorkLoop()!=self->loop)break;
            unsigned empty=8;
            for(unsigned i=0;i<8;i++) {
                if(self->bindings[i]==c->backend)return kIOReturnSuccess;
                if(!self->bindings[i])empty=i;
            }
            if(empty==8)break;
            c->result=ssb_init(c->backend,&self->manager,c->owner,c->platform,c->request,c->resultOps);
            if(!c->result)self->bindings[empty]=c->backend;
            break;
        }
        case Unbind:
            if(!c->backend)break;
            for(unsigned i=0;i<8;i++)if(self->bindings[i]==c->backend) {
                for(unsigned r=0;r<ST_RESOURCE_COUNT;r++)if(c->backend->bundle[r].token)return kIOReturnSuccess;
                c->result=ssb_detach(c->backend);
                if(!c->result)self->bindings[i]=nullptr;break;
            }
            break;
        case Close:
            for(unsigned i=0;i<8;i++)if(self->bindings[i])return kIOReturnSuccess;
            for(unsigned i=0;i<SR_LEASES;i++)if(self->manager.lease[i].state)return kIOReturnSuccess;
            for(unsigned i=0;i<SR_RESOURCES;i++)if(self->manager.resource[i].frozen)return kIOReturnSuccess;
            __atomic_store_n(&self->closed,1,__ATOMIC_RELEASE);
            c->dropPin=self->pinned;self->pinned=false;c->result=ST_OK;break;
        }
        return kIOReturnSuccess;
    }
public:
    bool initRegistry(IOService *owner,IOWorkLoop *workloop,OSObject *providerOwner,const sr_provider *provider) {
        if(!owner || !workloop || !providerOwner || owner->getWorkLoop()!=workloop || !OSObject::init())return false;
        heldOwner=owner;heldOwner->retain();loop=workloop;loop->retain();
        heldProvider=providerOwner;heldProvider->retain();
        sr_provider unavailable={};if(!provider){sr_unavailable_provider(&unavailable);provider=&unavailable;}
        sr_init(&manager,provider);
        retain();pinned=true;return true; // survives failed close; never auto-destroys unresolved leases
    }
    int invoke(RegistryCall &call) {
        if(!loop)return ST_BLOCKED;
        call.result=ST_BLOCKED;
        return loop->runAction(action,this,&call)==kIOReturnSuccess?call.result:ST_IO;
    }
    int closeRegistry() {
        RegistryCall c={};c.op=Close;int result=invoke(c);
        if(!result && c.dropPin)release();return result;
    }
    bool isClosed() const {return __atomic_load_n(&closed,__ATOMIC_ACQUIRE)!=0;}
    void free() override {OSSafeReleaseNULL(heldProvider);OSSafeReleaseNULL(heldOwner);OSSafeReleaseNULL(loop);OSObject::free();}
};
OSDefineMetaClassAndStructors(FuxiSharedRegistry,OSObject)
extern "C" FuxiSharedRegistry *fuxi_shared_create(IOService *owner,IOWorkLoop *loop,OSObject *providerOwner,const sr_provider *provider) {
    FuxiSharedRegistry *r=new FuxiSharedRegistry;
    if(r && !r->initRegistry(owner,loop,providerOwner,provider)){r->release();r=nullptr;}return r;
}
extern "C" int fuxi_shared_import(FuxiSharedRegistry *r,unsigned resource) {
    RegistryCall c={};c.op=Import;c.resource=resource;return r?r->invoke(c):ST_ARGUMENT;
}
extern "C" int fuxi_shared_acquire(FuxiSharedRegistry *r,unsigned resource,uint32_t owner,const sr_request *request,uintptr_t *token) {
    if(token)*token=0;RegistryCall c={};c.op=Acquire;c.resource=resource;c.owner=owner;c.request=request;c.resultToken=token;return r?r->invoke(c):ST_ARGUMENT;
}
extern "C" int fuxi_shared_release(FuxiSharedRegistry *r,uint32_t owner,uintptr_t token) {
    RegistryCall c={};c.op=Release;c.owner=owner;c.token=token;return r?r->invoke(c):ST_ARGUMENT;
}
extern "C" int fuxi_shared_reconcile(FuxiSharedRegistry *r,unsigned resource) {
    RegistryCall c={};c.op=Reconcile;c.resource=resource;return r?r->invoke(c):ST_ARGUMENT;
}
extern "C" int fuxi_shared_bind(FuxiSharedRegistry *r,st_shared_backend *backend,uint32_t owner,const st_board_ops *platform,const sr_request *demand,st_board_ops *out,IOService *hid) {
    RegistryCall c={};c.op=Bind;c.backend=backend;c.owner=owner;c.platform=platform;c.request=demand;c.resultOps=out;c.hid=hid;return r?r->invoke(c):ST_ARGUMENT;
}
extern "C" int fuxi_shared_unbind(FuxiSharedRegistry *r,st_shared_backend *backend) {
    RegistryCall c={};c.op=Unbind;c.backend=backend;return r?r->invoke(c):ST_ARGUMENT;
}
extern "C" int fuxi_shared_close(FuxiSharedRegistry *r) {return r?r->closeRegistry():ST_ARGUMENT;}
extern "C" void fuxi_shared_destroy_closed(FuxiSharedRegistry *r) {
    if(r && r->isClosed())r->release(); // caller owns exactly the factory reference
}
