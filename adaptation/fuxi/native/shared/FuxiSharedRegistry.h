/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_SHARED_REGISTRY_H
#define FUXI_SHARED_REGISTRY_H
#include "shared_board_backend.h"
class IOService;
class IOWorkLoop;
class OSObject;
class FuxiSharedRegistry;
extern "C" {
FuxiSharedRegistry *fuxi_shared_create(IOService *,IOWorkLoop *,OSObject *,const sr_provider *);
int fuxi_shared_import(FuxiSharedRegistry *,unsigned);
int fuxi_shared_acquire(FuxiSharedRegistry *,unsigned,uint32_t,const sr_request *,uintptr_t *);
int fuxi_shared_release(FuxiSharedRegistry *,uint32_t,uintptr_t);
int fuxi_shared_reconcile(FuxiSharedRegistry *,unsigned);
int fuxi_shared_bind(FuxiSharedRegistry *,st_shared_backend *,uint32_t,const st_board_ops *,const sr_request *,st_board_ops *,IOService *);
int fuxi_shared_unbind(FuxiSharedRegistry *,st_shared_backend *);
int fuxi_shared_close(FuxiSharedRegistry *);
void fuxi_shared_destroy_closed(FuxiSharedRegistry *);
}
#endif
