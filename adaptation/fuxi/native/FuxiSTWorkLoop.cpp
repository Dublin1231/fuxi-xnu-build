/* SPDX-License-Identifier: BSD-2-Clause
 * IOKit adapter SOURCE ONLY. Not SDK-compiled, linked, registered or booted.
 * The owning IOService must keep backend leases alive until stop_gated succeeds.
 * There is deliberately no MMIO backend, IRQ registration or service personality.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOReturn.h>
#include "board_lifecycle.h"

class FuxiSTEventSource : public IOEventSource {
    OSDeclareDefaultStructors(FuxiSTEventSource)
    st_board *board=nullptr;
    IOService *heldOwner=nullptr;
    unsigned queued=0,wakeEnabled=0;
public:
    bool initForBoard(IOService *owner,st_board *value) {
        if(!owner || !value || !IOEventSource::init(owner,nullptr))return false;
        heldOwner=owner;heldOwner->retain();board=value;
        __atomic_store_n(&wakeEnabled,1,__ATOMIC_RELEASE);return true;
    }
    // Called through the backend's ISR-safe wake callback. The shared dispatcher
    // must have been synchronized before detaching or releasing this object.
    void signal() {
        if(!__atomic_load_n(&wakeEnabled,__ATOMIC_ACQUIRE))return;
        __atomic_store_n(&queued,1,__ATOMIC_RELEASE);signalWorkAvailable();
    }
    bool checkForWork() override {
        if(__atomic_exchange_n(&queued,0,__ATOMIC_ACQ_REL))
            (void)st_board_worker(board); // workloop gate is held by the framework
        return false; // no busy re-polling on a permanently asserted line
    }
    void disableWake() {
        __atomic_store_n(&wakeEnabled,0,__ATOMIC_RELEASE);
        __atomic_store_n(&queued,0,__ATOMIC_RELEASE);
    }
    void free() override {
        OSSafeReleaseNULL(heldOwner);IOEventSource::free();
    }
};
OSDefineMetaClassAndStructors(FuxiSTEventSource,IOEventSource)

extern "C" IOEventSource *fuxi_st_create_event_source(IOService *owner,st_board *board) {
    FuxiSTEventSource *source=new FuxiSTEventSource;
    if(source && !source->initForBoard(owner,board)){source->release();source=nullptr;}
    return source; // caller adds to ONE workloop and retains until detach succeeds
}
extern "C" void fuxi_st_signal_event_source(IOEventSource *source) {
    // Factory-owned type; no runtime cast/lookup/allocation in the top half.
    if(source)static_cast<FuxiSTEventSource *>(source)->signal();
}
static IOReturn startAction(OSObject *,void *arg0,void *arg1,void *,void *) {
    *static_cast<int *>(arg1)=st_board_start(static_cast<st_board *>(arg0));
    return kIOReturnSuccess; // production start remains ST_BLOCKED
}
static IOReturn stopAction(OSObject *,void *arg0,void *arg1,void *,void *) {
    *static_cast<int *>(arg1)=st_board_stop(static_cast<st_board *>(arg0));
    return kIOReturnSuccess;
}
extern "C" int fuxi_st_start_gated(IOWorkLoop *loop,IOService *owner,st_board *board) {
    if(!loop || !owner || !board)return ST_ARGUMENT;
    int result=ST_BLOCKED;
    return loop->runAction(startAction,owner,board,&result)==kIOReturnSuccess?result:ST_IO;
}
extern "C" int fuxi_st_stop_gated(IOWorkLoop *loop,IOService *owner,st_board *board) {
    if(!loop || !owner || !board)return ST_ARGUMENT;
    int result=ST_BLOCKED;
    return loop->runAction(stopAction,owner,board,&result)==kIOReturnSuccess?result:ST_IO;
}
static IOReturn detachAction(OSObject *owner,void *arg0,void *arg1,void *arg2,void *) {
    FuxiSTEventSource *source=static_cast<FuxiSTEventSource *>(owner);
    st_board *board=static_cast<st_board *>(arg1);
    int *result=static_cast<int *>(arg2);*result=ST_BLOCKED;
    for(unsigned i=0;i<ST_RESOURCE_COUNT;i++)if(board->lease[i])return kIOReturnSuccess;
    source->disableWake();static_cast<IOWorkLoop *>(arg0)->removeEventSource(source);
    *result=ST_OK;return kIOReturnSuccess;
}
/* Failed stop leaves source/owner/backend retained for a later retry. */
extern "C" int fuxi_st_detach_source_gated(IOWorkLoop *loop,IOEventSource *source,st_board *board) {
    if(!loop || !source || !board || source->getWorkLoop()!=loop)return ST_ARGUMENT;
    int result=ST_BLOCKED;
    return loop->runAction(detachAction,source,loop,board,&result)==kIOReturnSuccess?result:ST_IO;
}
