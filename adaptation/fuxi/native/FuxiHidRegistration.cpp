/* SPDX-License-Identifier: BSD-2-Clause */
#include "FuxiSTService.h"
#include <libkern/c++/OSSymbol.h>
#include <mach/kmod.h>
extern kmod_info_t g_kernel_kmod_info;
extern const OSMetaClass fuxi_original_hid_meta __asm__("__ZN11IOHIDDevice10gMetaClassE");
extern "C" void fuxi_hid_constructor(void) __asm__("__GLOBAL__sub_I_NativeHidInput.cpp");
/* No static initializer, kext load request, boot argument or implicit retry.
 * pre/postModLoad are paired. A failed registration is terminal for this boot:
 * running the C++ initializer again would register the same metaclass twice.
 */
static unsigned registrationState;
extern "C" int native_fuxi_st_register_hid(void) {
    if(native_fuxi_st_boot_policy()!=ST_OK)return ST_BLOCKED;
    const OSSymbol *name=OSSymbol::withCString("IOHIDDevice");
    const OSMetaClass *meta=name?OSMetaClass::getMetaClassWithName(name):nullptr;
    OSSafeReleaseNULL(name);
    if(meta!=&fuxi_original_hid_meta || !meta || meta->getClassSize()!=208)return ST_BLOCKED;
    unsigned expected=0;
    if(!__atomic_compare_exchange_n(&registrationState,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
        return expected==2?ST_OK:ST_BLOCKED;
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if(!handle){__atomic_store_n(&registrationState,3,__ATOMIC_RELEASE);return ST_IO;}
    fuxi_hid_constructor();
    int result=OSMetaClass::postModLoad(handle)==kIOReturnSuccess?ST_OK:ST_IO;
    __atomic_store_n(&registrationState,result==ST_OK?2u:3u,__ATOMIC_RELEASE);
    return result;
}
