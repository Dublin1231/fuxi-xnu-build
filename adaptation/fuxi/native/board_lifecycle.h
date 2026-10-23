/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_BOARD_LIFECYCLE_H
#define FUXI_BOARD_LIFECYCLE_H
#include "fuxi_st_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
enum st_board_state { ST_BOARD_NEW,ST_BOARD_PREPARING,ST_BOARD_RUNNING,
    ST_BOARD_QUIESCING,ST_BOARD_STOPPED,ST_BOARD_FAULT,ST_BOARD_SUSPENDED };
enum st_resource { ST_VDD,ST_AVDD,ST_INTERCONNECT,ST_CLOCK_M,ST_CLOCK_S,ST_CLOCK_SE,
    ST_PINCTRL,ST_BUS,ST_HID,ST_IRQ,ST_RESOURCE_COUNT };
/* Every token is a scoped lease. Release removes this consumer's vote/reference
 * only, never blindly disables a shared rail/branch or rewrites an SMMU stream.
 * On partial acquisition failure return a nonzero token for rollback as well.
 * IRQ acquisition registers ONLY GPIO25 with a shared dispatcher, initially masked.
 * Quiesce joins in-flight top halves; it must not wait on this workloop's worker.
 * claim/mask/wake are ISR-safe and allocation-free; all others are worker-only.
 * Control methods and worker must be serialized by ONE platform command gate.
 * Caller holds object/backend lifetime until mask+quiesce has succeeded.
 */
struct st_board_ops {
    void *ctx;
    int (*acquire)(void *,enum st_resource,uintptr_t *);
    int (*release)(void *,enum st_resource,uintptr_t);
    int (*qualify_controller)(void *);
    int (*irq_claim)(void *);       /* 1: this pin; 0: shared IRQ is not ours */
    int (*irq_mask)(void *);
    int (*irq_quiesce)(void *);
    int (*irq_ack)(void *);         /* pin latch only, not global GIC EOI */
    int (*irq_unmask)(void *);
    int (*irq_active)(void *);      /* 1 = GPIO25 low, 0 = deasserted */
    void (*wake_worker)(void *);
    st_exchange exchange;
    st_deliver deliver;
    void *hid;
};
struct st_board {
    struct st_board_ops ops;
    struct st_bridge bridge;
    uintptr_t lease[ST_RESOURCE_COUNT];
    unsigned accepting,pending,async_error,empty_rounds;
    enum st_board_state state;
    int last_error,cleanup_error,hid_released;
};
void st_board_init(struct st_board *,const struct st_board_ops *);
/* Production gate is unconditionally closed, with ZERO backend calls. */
int st_board_start(struct st_board *);
int st_board_stop(struct st_board *);
int st_board_suspend(struct st_board *);
int st_board_resume(struct st_board *);
int st_board_irq(struct st_board *);
int st_board_worker(struct st_board *);
/* Symbol is absent from production objects; only synthetic host tests enable it. */
#ifdef ST_BOARD_HOST_TEST
int st_board_start_offline(struct st_board *);
#endif
#ifdef __cplusplus
}
#endif
#endif
