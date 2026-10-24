/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_ST_SERVICE_H
#define FUXI_ST_SERVICE_H
#include <IOKit/IOService.h>
#include "shared/FuxiSharedRegistry.h"
#include "FuxiSTWorkLoop.h"
/* No concrete hardware provider exists. Implementations must hold all callback
 * owners until cleanup succeeds, join the IRQ dispatcher in irq_quiesce, and
 * export only audited shared-resource requests. Export itself performs no IO.
 * The IRQ dispatcher calls st_board_irq(board); wake only signals its worker.
 */
class FuxiSTHardwareProvider : public IOService {
    OSDeclareAbstractStructors(FuxiSTHardwareProvider)
public:
    virtual bool copyResources(IOService *consumer,IOService *hid,IOWorkLoop *,
        st_board *,void (*wake)(void *),void *wakeContext,sr_provider *,
        st_board_ops *,sr_request demand[SR_RESOURCES])=0;
};
extern "C" int native_fuxi_st_register_hid(void);
extern "C" int native_fuxi_st_boot_policy(void);
#endif
