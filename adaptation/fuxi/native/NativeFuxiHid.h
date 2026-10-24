/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef NATIVE_FUXI_HID_H
#define NATIVE_FUXI_HID_H
#include "fuxi_st_bridge.h"
class IOService;
/* Factory is never called by static initialization. Future board owner must
 * retain the service and quiesce the producer before stop/free. */
extern "C" IOService *native_fuxi_st_hid_allocate(void);
extern "C" int native_fuxi_st_deliver(void *,const uint8_t *,size_t);
static_assert(__is_same(decltype(&native_fuxi_st_deliver),st_deliver),"Synchronous HID callback ABI");
#endif
