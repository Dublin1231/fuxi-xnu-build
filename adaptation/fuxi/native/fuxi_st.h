/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_ST_H
#define FUXI_ST_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FUXI_ST_CONTACTS 10
#define FUXI_ST_DEPTH 32
#define FUXI_ST_REPORT_BYTES 62
enum { ST_OK=0, ST_ARGUMENT=-1, ST_PROTOCOL=-2, ST_UNSUPPORTED=-3,
       ST_IO=-4, ST_REINIT=-5, ST_BLOCKED=-6 };
struct st_contact { uint16_t x,y; uint8_t down; };
struct st_state { struct st_contact contact[FUXI_ST_CONTACTS]; uint8_t reinit; };
struct st_reports { uint8_t bytes[FUXI_ST_DEPTH][FUXI_ST_REPORT_BYTES]; size_t count; };
/* Command and receive phases MUST share one chip-select assertion. rx includes
 * one leading dummy byte. Return 0 only after receiving EXACTLY rx_len bytes.
 * FIFO consumption is destructive: implementations must NOT retry a failed read.
 * Caller serializes against all other bus users; no hard-IRQ execution. */
typedef int (*st_exchange)(void *ctx, uint8_t command, uint8_t *rx, size_t rx_len);
void fuxi_st_init(struct st_state *state);
int fuxi_st_read_fifo(st_exchange exchange, void *ctx, uint8_t events[32][8], size_t *count);
/* Delta events, not full contact snapshots. Atomic validation on malformed or
 * unsupported input. Ready/error releases contacts and returns ST_REINIT.
 * Finger type 1 only; glove, palm, hover and vendor reports are unqualified. */
int fuxi_st_decode(struct st_state *state, const uint8_t events[][8], size_t count,
                   struct st_reports *out);
int fuxi_st_cancel(struct st_state *state, struct st_reports *out);
/* No runtime-enabling switch: board clocks/pins/SMMU/IRQ are not qualified. */
int fuxi_st_board_start(void);
#ifdef __cplusplus
}
#endif
#endif
