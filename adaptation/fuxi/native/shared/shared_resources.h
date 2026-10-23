/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_SHARED_RESOURCES_H
#define FUXI_SHARED_RESOURCES_H
#include "../board_lifecycle.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SR_RESOURCES 8
#define SR_LEASES 32
enum sr_resource { SR_VDD,SR_AVDD,SR_CLOCK_M,SR_CLOCK_S,SR_CLOCK_SE,SR_ICC_CORE,SR_ICC_CONFIG,SR_ICC_MEMORY };
enum sr_kind { SR_RANGE,SR_BANDWIDTH };
enum sr_commit { SR_APPLIED,SR_UNCHANGED,SR_INDETERMINATE };
struct sr_vote { unsigned enabled; uint64_t minimum,maximum,average,peak; };
struct sr_request { struct sr_vote active,sleep; };
/* Provider MUST serialize compare-and-apply with every other consumer of the
 * physical resource. Snapshot includes aggregate constraints, not just measured
 * voltage. No guessed baseline/command DB, global clock disable or SMMU bypass.
 * qualified==1 attests both active/sleep state and ownership/aggregation policy.
 * apply: APPLIED means verified readback; UNCHANGED means no side effects;
 * INDETERMINATE freezes that resource until observe matches old or desired.
 * All APIs execute under one external gate, never in interrupt context.
 */
struct sr_provider {
    void *ctx;
    int (*qualified)(void *,unsigned);
    int (*observe)(void *,unsigned,struct sr_request *);
    enum sr_commit (*apply)(void *,unsigned,const struct sr_request *,const struct sr_request *);
};
struct sr_entry {
    struct sr_request baseline,current,desired;
    unsigned known,frozen,pending_slot,pending_remove;
};
struct sr_lease {
    struct sr_request request;
    uintptr_t token;
    uint32_t owner;
    unsigned resource,state;
};
struct sr_manager {
    struct sr_provider provider;
    struct sr_entry resource[SR_RESOURCES];
    struct sr_lease lease[SR_LEASES];
    uintptr_t generation;
};
void sr_init(struct sr_manager *,const struct sr_provider *);
int sr_import_baseline(struct sr_manager *,unsigned);
int sr_acquire(struct sr_manager *,unsigned,uint32_t,const struct sr_request *,uintptr_t *);
int sr_release(struct sr_manager *,uint32_t,uintptr_t);
int sr_reconcile(struct sr_manager *,unsigned);
/* No physical provider exists in this change. Production defaults stay closed. */
void sr_unavailable_provider(struct sr_provider *);
#ifdef __cplusplus
}
#endif
#endif
