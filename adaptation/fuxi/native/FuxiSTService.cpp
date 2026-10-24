/* SPDX-License-Identifier: BSD-2-Clause */
#include "FuxiSTService.h"
#include "NativeFuxiHid.h"
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOEventSource.h>
#include <IOKit/IOLocks.h>
struct FuxiControlGuard {
    IORecursiveLock *lock;
    explicit FuxiControlGuard(IORecursiveLock *value):lock(value){IORecursiveLockLock(lock);}
    ~FuxiControlGuard(){IORecursiveLockUnlock(lock);}
};
OSDefineMetaClassAndAbstractStructors(FuxiSTHardwareProvider,IOService)
/* Separate from the board's closed start gate. No runtime/boot-arg bypass. */
extern "C" int native_fuxi_st_boot_policy(void) {return ST_BLOCKED;}

class FuxiSTService : public IOService {
    OSDeclareDefaultStructors(FuxiSTService)
    IORecursiveLock *control=nullptr;
    FuxiSTHardwareProvider *hardware=nullptr;
    IOService *hid=nullptr;
    IOWorkLoop *loop=nullptr;
    IOEventSource *source=nullptr;
    FuxiSharedRegistry *registry=nullptr;
    st_shared_backend backend={};
    st_board board={};
    bool opened=false,attached=false,hidStarted=false,sourceAdded=false;
    bool bound=false,pinned=false;
    static void wake(void *context) {
        FuxiSTService *self=static_cast<FuxiSTService *>(context);
        fuxi_st_signal_event_source(self->source);
    }
    bool cleanup() {
        if(loop && fuxi_st_stop_gated(loop,this,&board)!=ST_OK)return false;
        if(sourceAdded) {
            if(fuxi_st_detach_source_gated(loop,source,&board)!=ST_OK)return false;
            sourceAdded=false;
        }
        if(bound) {
            if(fuxi_shared_unbind(registry,&backend)!=ST_OK)return false;
            bound=false;
        }
        if(registry) {
            if(fuxi_shared_close(registry)!=ST_OK)return false;
            fuxi_shared_destroy_closed(registry);registry=nullptr;
        }
        // Board stop has already quiesced IRQs and sent explicit tip-up reports.
        // HID stays accepting until every resource release above succeeded.
        OSSafeReleaseNULL(source);
        if(hidStarted){hid->stop(this);hidStarted=false;}
        if(attached){hid->detach(this);attached=false;}
        OSSafeReleaseNULL(hid);OSSafeReleaseNULL(loop);
        if(opened){hardware->close(this);opened=false;}
        OSSafeReleaseNULL(hardware);
        if(pinned){pinned=false;release();}
        return true;
    }
public:
    bool init(OSDictionary *dictionary=nullptr) override {
        if(!IOService::init(dictionary))return false;
        control=IORecursiveLockAlloc();return control!=nullptr;
    }
    void free() override {
        if(control)IORecursiveLockFree(control);
        IOService::free();
    }
    IOWorkLoop *getWorkLoop() const override {return loop;}
    bool start(IOService *provider) override {
        FuxiControlGuard guard(control);
        // Fail before base start, allocation, provider callback or HID registration.
        if(native_fuxi_st_boot_policy()!=ST_OK || pinned || hardware)return false;
        FuxiSTHardwareProvider *candidate=OSDynamicCast(FuxiSTHardwareProvider,provider);
        if(!candidate || !IOService::start(provider))return false;
        retain();pinned=true;hardware=candidate;hardware->retain();
        if(!hardware->open(this))goto failed;
        opened=true;
        if(native_fuxi_st_register_hid()!=ST_OK)goto failed;
        hid=native_fuxi_st_hid_allocate();
        if(!hid || !hid->init() || !(attached=hid->attach(this)))goto failed;
        if(!(hidStarted=hid->start(this)))goto failed;
        loop=hid->getWorkLoop();if(!loop)goto failed;loop->retain();
        source=fuxi_st_create_event_source(this,&board);
        if(!source || loop->addEventSource(source)!=kIOReturnSuccess)goto failed;
        sourceAdded=true;
        {
            sr_provider resources={};st_board_ops platform={},ops={};
            sr_request demand[SR_RESOURCES]={};
            if(!hardware->copyResources(this,hid,loop,&board,wake,this,&resources,&platform,demand))goto failed;
            platform.hid=hid;platform.deliver=native_fuxi_st_deliver;
            registry=fuxi_shared_create(this,loop,hardware,&resources);
            if(!registry)goto failed;
            for(unsigned i=0;i<SR_RESOURCES;++i)if(fuxi_shared_import(registry,i)!=ST_OK)goto failed;
            if(fuxi_shared_bind(registry,&backend,1,&platform,demand,&ops,hid)!=ST_OK)goto failed;
            bound=true;st_board_init(&board,&ops);
        }
        if(fuxi_st_start_gated(loop,this,&board)!=ST_OK)goto failed;
        registerService();return true;
    failed:
        if(!cleanup())setProperty("FuxiCleanupBlocked",true);
        IOService::stop(provider);return false;
    }
    void stop(IOService *provider) override {
        FuxiControlGuard guard(control);
        if(!cleanup())setProperty("FuxiCleanupBlocked",true);
        IOService::stop(provider);
    }
    bool willTerminate(IOService *provider,IOOptionBits options) override {
        FuxiControlGuard guard(control);
        if(!cleanup())return false;
        return IOService::willTerminate(provider,options);
    }
};
OSDefineMetaClassAndStructors(FuxiSTService,IOService)
