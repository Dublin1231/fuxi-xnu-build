/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_ST_BRIDGE_H
#define FUXI_ST_BRIDGE_H
#include "fuxi_st.h"
#ifdef __cplusplus
extern "C" {
#endif
/* XNU consumer runs this synchronous batch on the HID device's IOWorkLoop.
 * The bytes are borrowed until return. 0 means ALL reports accepted; errors
 * quarantine the producer because acceptance might have been partial. */
typedef int (*st_deliver)(void *device, const uint8_t *reports, size_t count);
struct st_bridge { struct st_state state; int faulted; };
void fuxi_st_bridge_init(struct st_bridge *bridge);
int fuxi_st_bridge_events(struct st_bridge *bridge, const uint8_t events[][8],
                          size_t count, st_deliver deliver, void *device);
int fuxi_st_bridge_poll(struct st_bridge *bridge, st_exchange exchange, void *bus,
                        st_deliver deliver, void *device);
/* Explicit all-ID tip-up after sink/transport failure. Call on the same workloop
 * before detaching the sink. Does not clear quarantine or reinitialize the IC. */
int fuxi_st_bridge_release(struct st_bridge *bridge, st_deliver deliver, void *device);
#ifdef __cplusplus
}
#endif
#endif
