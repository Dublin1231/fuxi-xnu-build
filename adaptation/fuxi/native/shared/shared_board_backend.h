/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_SHARED_BOARD_BACKEND_H
#define FUXI_SHARED_BOARD_BACKEND_H
#include "shared_resources.h"
#ifdef __cplusplus
extern "C" {
#endif
struct ssb_bundle { uintptr_t token,part[3]; unsigned count,shared; };
struct st_shared_backend {
    struct sr_manager *manager;
    struct st_board_ops platform;
    struct sr_request demand[SR_RESOURCES];
    struct ssb_bundle bundle[ST_RESOURCE_COUNT];
    uintptr_t generation;
    uint32_t owner;
};
/* Backend storage MUST be zero-initialized before its first bind.
 * Demand values come from a qualified platform plan, NEVER this file's defaults.
 * Rebinding requires explicit detach; generations survive detach/rebind.
 * manager and every platform/HID method MUST share the same workloop gate.
 */
int ssb_init(struct st_shared_backend *,struct sr_manager *,uint32_t,
              const struct st_board_ops *,const struct sr_request demand[SR_RESOURCES],struct st_board_ops *out);
int ssb_detach(struct st_shared_backend *);
#ifdef __cplusplus
}
#endif
#endif
