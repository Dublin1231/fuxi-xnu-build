/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_ST_WORK_LOOP_H
#define FUXI_ST_WORK_LOOP_H
#include "board_lifecycle.h"
class IOEventSource;
class IOService;
class IOWorkLoop;
extern "C" {
IOEventSource *fuxi_st_create_event_source(IOService *,st_board *);
void fuxi_st_signal_event_source(IOEventSource *);
int fuxi_st_start_gated(IOWorkLoop *,IOService *,st_board *);
int fuxi_st_stop_gated(IOWorkLoop *,IOService *,st_board *);
int fuxi_st_detach_source_gated(IOWorkLoop *,IOEventSource *,st_board *);
}
#endif
